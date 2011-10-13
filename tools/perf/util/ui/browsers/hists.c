#define _GNU_SOURCE
#include <stdio.h>
#undef _GNU_SOURCE
#include "../libslang.h"
#include <stdlib.h>
#include <string.h>
#include <newt.h>
#include <linux/rbtree.h>

#include "../../evsel.h"
#include "../../evlist.h"
#include "../../hist.h"
#include "../../pstack.h"
#include "../../sort.h"
#include "../../util.h"

#include "../browser.h"
#include "../helpline.h"
#include "../util.h"
#include "map.h"

struct hist_browser {
	struct ui_browser   b;
	struct hists	    *hists;
	struct hist_entry   *he_selection;
	struct map_symbol   *selection;
	const struct thread *thread_filter;
	const struct dso    *dso_filter;
	bool		     has_symbols;
};

static int hists__browser_title(struct hists *self, char *bf, size_t size,
				const char *ev_name, const struct dso *dso,
				const struct thread *thread);

static void hist_browser__refresh_dimensions(struct hist_browser *self)
{
	/* 3 == +/- toggle symbol before actual hist_entry rendering */
	self->b.width = 3 + (hists__sort_list_width(self->hists) +
			     sizeof("[k]"));
}

static void hist_browser__reset(struct hist_browser *self)
{
	self->b.nr_entries = self->hists->nr_entries;
	hist_browser__refresh_dimensions(self);
	ui_browser__reset_index(&self->b);
}

static char tree__folded_sign(bool unfolded)
{
	return unfolded ? '-' : '+';
}

static char map_symbol__folded(const struct map_symbol *self)
{
	return self->has_children ? tree__folded_sign(self->unfolded) : ' ';
}

static char hist_entry__folded(const struct hist_entry *self)
{
	return map_symbol__folded(&self->ms);
}

static char callchain_list__folded(const struct callchain_list *self)
{
	return map_symbol__folded(&self->ms);
}

static void map_symbol__set_folding(struct map_symbol *self, bool unfold)
{
	self->unfolded = unfold ? self->has_children : false;
}

static int callchain_node__count_rows_rb_tree(struct callchain_node *self)
{
	int n = 0;
	struct rb_node *nd;

	for (nd = rb_first(&self->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		char folded_sign = ' '; /* No children */

		list_for_each_entry(chain, &child->val, list) {
			++n;
			/* We need this because we may not have children */
			folded_sign = callchain_list__folded(chain);
			if (folded_sign == '+')
				break;
		}

		if (folded_sign == '-') /* Have children and they're unfolded */
			n += callchain_node__count_rows_rb_tree(child);
	}

	return n;
}

static int callchain_node__count_rows(struct callchain_node *node)
{
	struct callchain_list *chain;
	bool unfolded = false;
	int n = 0;

	list_for_each_entry(chain, &node->val, list) {
		++n;
		unfolded = chain->ms.unfolded;
	}

	if (unfolded)
		n += callchain_node__count_rows_rb_tree(node);

	return n;
}

static int callchain__count_rows(struct rb_root *chain)
{
	struct rb_node *nd;
	int n = 0;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		n += callchain_node__count_rows(node);
	}

	return n;
}

static bool map_symbol__toggle_fold(struct map_symbol *self)
{
	if (!self->has_children)
		return false;

	self->unfolded = !self->unfolded;
	return true;
}

static void callchain_node__init_have_children_rb_tree(struct callchain_node *self)
{
	struct rb_node *nd = rb_first(&self->rb_root);

	for (nd = rb_first(&self->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		bool first = true;

		list_for_each_entry(chain, &child->val, list) {
			if (first) {
				first = false;
				chain->ms.has_children = chain->list.next != &child->val ||
							 !RB_EMPTY_ROOT(&child->rb_root);
			} else
				chain->ms.has_children = chain->list.next == &child->val &&
							 !RB_EMPTY_ROOT(&child->rb_root);
		}

		callchain_node__init_have_children_rb_tree(child);
	}
}

static void callchain_node__init_have_children(struct callchain_node *self)
{
	struct callchain_list *chain;

	list_for_each_entry(chain, &self->val, list)
		chain->ms.has_children = !RB_EMPTY_ROOT(&self->rb_root);

	callchain_node__init_have_children_rb_tree(self);
}

static void callchain__init_have_children(struct rb_root *self)
{
	struct rb_node *nd;

	for (nd = rb_first(self); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		callchain_node__init_have_children(node);
	}
}

static void hist_entry__init_have_children(struct hist_entry *self)
{
	if (!self->init_have_children) {
		self->ms.has_children = !RB_EMPTY_ROOT(&self->sorted_chain);
		callchain__init_have_children(&self->sorted_chain);
		self->init_have_children = true;
	}
}

static bool hist_browser__toggle_fold(struct hist_browser *self)
{
	if (map_symbol__toggle_fold(self->selection)) {
		struct hist_entry *he = self->he_selection;

		hist_entry__init_have_children(he);
		self->hists->nr_entries -= he->nr_rows;

		if (he->ms.unfolded)
			he->nr_rows = callchain__count_rows(&he->sorted_chain);
		else
			he->nr_rows = 0;
		self->hists->nr_entries += he->nr_rows;
		self->b.nr_entries = self->hists->nr_entries;

		return true;
	}

	/* If it doesn't have children, no toggling performed */
	return false;
}

static int callchain_node__set_folding_rb_tree(struct callchain_node *self, bool unfold)
{
	int n = 0;
	struct rb_node *nd;

	for (nd = rb_first(&self->rb_root); nd; nd = rb_next(nd)) {
		struct callchain_node *child = rb_entry(nd, struct callchain_node, rb_node);
		struct callchain_list *chain;
		bool has_children = false;

		list_for_each_entry(chain, &child->val, list) {
			++n;
			map_symbol__set_folding(&chain->ms, unfold);
			has_children = chain->ms.has_children;
		}

		if (has_children)
			n += callchain_node__set_folding_rb_tree(child, unfold);
	}

	return n;
}

static int callchain_node__set_folding(struct callchain_node *node, bool unfold)
{
	struct callchain_list *chain;
	bool has_children = false;
	int n = 0;

	list_for_each_entry(chain, &node->val, list) {
		++n;
		map_symbol__set_folding(&chain->ms, unfold);
		has_children = chain->ms.has_children;
	}

	if (has_children)
		n += callchain_node__set_folding_rb_tree(node, unfold);

	return n;
}

static int callchain__set_folding(struct rb_root *chain, bool unfold)
{
	struct rb_node *nd;
	int n = 0;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);
		n += callchain_node__set_folding(node, unfold);
	}

	return n;
}

static void hist_entry__set_folding(struct hist_entry *self, bool unfold)
{
	hist_entry__init_have_children(self);
	map_symbol__set_folding(&self->ms, unfold);

	if (self->ms.has_children) {
		int n = callchain__set_folding(&self->sorted_chain, unfold);
		self->nr_rows = unfold ? n : 0;
	} else
		self->nr_rows = 0;
}

static void hists__set_folding(struct hists *self, bool unfold)
{
	struct rb_node *nd;

	self->nr_entries = 0;

	for (nd = rb_first(&self->entries); nd; nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		hist_entry__set_folding(he, unfold);
		self->nr_entries += 1 + he->nr_rows;
	}
}

static void hist_browser__set_folding(struct hist_browser *self, bool unfold)
{
	hists__set_folding(self->hists, unfold);
	self->b.nr_entries = self->hists->nr_entries;
	/* Go to the start, we may be way after valid entries after a collapse */
	ui_browser__reset_index(&self->b);
}

static int hist_browser__run(struct hist_browser *self, const char *ev_name,
			     void(*timer)(void *arg), void *arg, int delay_secs)
{
	int key;
	int delay_msecs = delay_secs * 1000;
	char title[160];
	int sym_exit_keys[] = { 'a', 'h', 'C', 'd', 'E', 't', 0, };
	int exit_keys[] = { '?', 'h', 'D', NEWT_KEY_LEFT, NEWT_KEY_RIGHT,
			    NEWT_KEY_TAB, NEWT_KEY_UNTAB, NEWT_KEY_ENTER, 0, };

	self->b.entries = &self->hists->entries;
	self->b.nr_entries = self->hists->nr_entries;

	hist_browser__refresh_dimensions(self);
	hists__browser_title(self->hists, title, sizeof(title), ev_name,
			     self->dso_filter, self->thread_filter);

	if (ui_browser__show(&self->b, title,
			     "Press '?' for help on key bindings") < 0)
		return -1;

	if (timer != NULL)
		newtFormSetTimer(self->b.form, delay_msecs);

	ui_browser__add_exit_keys(&self->b, exit_keys);
	if (self->has_symbols)
		ui_browser__add_exit_keys(&self->b, sym_exit_keys);

	while (1) {
		key = ui_browser__run(&self->b);

		switch (key) {
		case -1:
			/* FIXME we need to check if it was es.reason == NEWT_EXIT_TIMER */
			timer(arg);
			ui_browser__update_nr_entries(&self->b, self->hists->nr_entries);
			hists__browser_title(self->hists, title, sizeof(title),
					     ev_name, self->dso_filter,
					     self->thread_filter);
			ui_browser__show_title(&self->b, title);
			continue;
		case 'D': { /* Debug */
			static int seq;
			struct hist_entry *h = rb_entry(self->b.top,
							struct hist_entry, rb_node);
			ui_helpline__pop();
			ui_helpline__fpush("%d: nr_ent=(%d,%d), height=%d, idx=%d, fve: idx=%d, row_off=%d, nrows=%d",
					   seq++, self->b.nr_entries,
					   self->hists->nr_entries,
					   self->b.height,
					   self->b.index,
					   self->b.top_idx,
					   h->row_offset, h->nr_rows);
		}
			break;
		case 'C':
			/* Collapse the whole world. */
			hist_browser__set_folding(self, false);
			break;
		case 'E':
			/* Expand the whole world. */
			hist_browser__set_folding(self, true);
			break;
		case NEWT_KEY_ENTER:
			if (hist_browser__toggle_fold(self))
				break;
			/* fall thru */
		default:
			goto out;
		}
	}
out:
	ui_browser__hide(&self->b);
	return key;
}

static char *callchain_list__sym_name(struct callchain_list *self,
				      char *bf, size_t bfsize)
{
	if (self->ms.sym)
		return self->ms.sym->name;

	snprintf(bf, bfsize, "%#" PRIx64, self->ip);
	return bf;
}

#define LEVEL_OFFSET_STEP 3

static int hist_browser__show_callchain_node_rb_tree(struct hist_browser *self,
						     struct callchain_node *chain_node,
						     u64 total, int level,
						     unsigned short row,
						     off_t *row_offset,
						     bool *is_current_entry)
{
	struct rb_node *node;
	int first_row = row, width, offset = level * LEVEL_OFFSET_STEP;
	u64 new_total, remaining;

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		new_total = chain_node->children_hit;
	else
		new_total = total;

	remaining = new_total;
	node = rb_first(&chain_node->rb_root);
	while (node) {
		struct callchain_node *child = rb_entry(node, struct callchain_node, rb_node);
		struct rb_node *next = rb_next(node);
		u64 cumul = callchain_cumul_hits(child);
		struct callchain_list *chain;
		char folded_sign = ' ';
		int first = true;
		int extra_offset = 0;

		remaining -= cumul;

		list_for_each_entry(chain, &child->val, list) {
			char ipstr[BITS_PER_LONG / 4 + 1], *alloc_str;
			const char *str;
			int color;
			bool was_first = first;

			if (first)
				first = false;
			else
				extra_offset = LEVEL_OFFSET_STEP;

			folded_sign = callchain_list__folded(chain);
			if (*row_offset != 0) {
				--*row_offset;
				goto do_next;
			}

			alloc_str = NULL;
			str = callchain_list__sym_name(chain, ipstr, sizeof(ipstr));
			if (was_first) {
				double percent = cumul * 100.0 / new_total;

				if (asprintf(&alloc_str, "%2.2f%% %s", percent, str) < 0)
					str = "Not enough memory!";
				else
					str = alloc_str;
			}

			color = HE_COLORSET_NORMAL;
			width = self->b.width - (offset + extra_offset + 2);
			if (ui_browser__is_current_entry(&self->b, row)) {
				self->selection = &chain->ms;
				color = HE_COLORSET_SELECTED;
				*is_current_entry = true;
			}

			ui_browser__set_color(&self->b, color);
			ui_browser__gotorc(&self->b, row, 0);
			slsmg_write_nstring(" ", offset + extra_offset);
			slsmg_printf("%c ", folded_sign);
			slsmg_write_nstring(str, width);
			free(alloc_str);

			if (++row == self->b.height)
				goto out;
do_next:
			if (folded_sign == '+')
				break;
		}

		if (folded_sign == '-') {
			const int new_level = level + (extra_offset ? 2 : 1);
			row += hist_browser__show_callchain_node_rb_tree(self, child, new_total,
									 new_level, row, row_offset,
									 is_current_entry);
		}
		if (row == self->b.height)
			goto out;
		node = next;
	}
out:
	return row - first_row;
}

static int hist_browser__show_callchain_node(struct hist_browser *self,
					     struct callchain_node *node,
					     int level, unsigned short row,
					     off_t *row_offset,
					     bool *is_current_entry)
{
	struct callchain_list *chain;
	int first_row = row,
	     offset = level * LEVEL_OFFSET_STEP,
	     width = self->b.width - offset;
	char folded_sign = ' ';

	list_for_each_entry(chain, &node->val, list) {
		char ipstr[BITS_PER_LONG / 4 + 1], *s;
		int color;

		folded_sign = callchain_list__folded(chain);

		if (*row_offset != 0) {
			--*row_offset;
			continue;
		}

		color = HE_COLORSET_NORMAL;
		if (ui_browser__is_current_entry(&self->b, row)) {
			self->selection = &chain->ms;
			color = HE_COLORSET_SELECTED;
			*is_current_entry = true;
		}

		s = callchain_list__sym_name(chain, ipstr, sizeof(ipstr));
		ui_browser__gotorc(&self->b, row, 0);
		ui_browser__set_color(&self->b, color);
		slsmg_write_nstring(" ", offset);
		slsmg_printf("%c ", folded_sign);
		slsmg_write_nstring(s, width - 2);

		if (++row == self->b.height)
			goto out;
	}

	if (folded_sign == '-')
		row += hist_browser__show_callchain_node_rb_tree(self, node,
								 self->hists->stats.total_period,
								 level + 1, row,
								 row_offset,
								 is_current_entry);
out:
	return row - first_row;
}

static int hist_browser__show_callchain(struct hist_browser *self,
					struct rb_root *chain,
					int level, unsigned short row,
					off_t *row_offset,
					bool *is_current_entry)
{
	struct rb_node *nd;
	int first_row = row;

	for (nd = rb_first(chain); nd; nd = rb_next(nd)) {
		struct callchain_node *node = rb_entry(nd, struct callchain_node, rb_node);

		row += hist_browser__show_callchain_node(self, node, level,
							 row, row_offset,
							 is_current_entry);
		if (row == self->b.height)
			break;
	}

	return row - first_row;
}

static int hist_browser__show_entry(struct hist_browser *self,
				    struct hist_entry *entry,
				    unsigned short row)
{
	char s[256];
	double percent;
	int printed = 0;
	int color, width = self->b.width;
	char folded_sign = ' ';
	bool current_entry = ui_browser__is_current_entry(&self->b, row);
	off_t row_offset = entry->row_offset;

	if (current_entry) {
		self->he_selection = entry;
		self->selection = &entry->ms;
	}

	if (symbol_conf.use_callchain) {
		hist_entry__init_have_children(entry);
		folded_sign = hist_entry__folded(entry);
	}

	if (row_offset == 0) {
		hist_entry__snprintf(entry, s, sizeof(s), self->hists, NULL, false,
				     0, false, self->hists->stats.total_period);
		percent = (entry->period * 100.0) / self->hists->stats.total_period;

		color = HE_COLORSET_SELECTED;
		if (!current_entry) {
			if (percent >= MIN_RED)
				color = HE_COLORSET_TOP;
			else if (percent >= MIN_GREEN)
				color = HE_COLORSET_MEDIUM;
			else
				color = HE_COLORSET_NORMAL;
		}

		ui_browser__set_color(&self->b, color);
		ui_browser__gotorc(&self->b, row, 0);
		if (symbol_conf.use_callchain) {
			slsmg_printf("%c ", folded_sign);
			width -= 2;
		}
		slsmg_write_nstring(s, width);
		++row;
		++printed;
	} else
		--row_offset;

	if (folded_sign == '-' && row != self->b.height) {
		printed += hist_browser__show_callchain(self, &entry->sorted_chain,
							1, row, &row_offset,
							&current_entry);
		if (current_entry)
			self->he_selection = entry;
	}

	return printed;
}

static unsigned int hist_browser__refresh(struct ui_browser *self)
{
	unsigned row = 0;
	struct rb_node *nd;
	struct hist_browser *hb = container_of(self, struct hist_browser, b);

	if (self->top == NULL)
		self->top = rb_first(&hb->hists->entries);

	for (nd = self->top; nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);

		if (h->filtered)
			continue;

		row += hist_browser__show_entry(hb, h, row);
		if (row == self->height)
			break;
	}

	return row;
}

static struct rb_node *hists__filter_entries(struct rb_node *nd)
{
	while (nd != NULL) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		if (!h->filtered)
			return nd;

		nd = rb_next(nd);
	}

	return NULL;
}

static struct rb_node *hists__filter_prev_entries(struct rb_node *nd)
{
	while (nd != NULL) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		if (!h->filtered)
			return nd;

		nd = rb_prev(nd);
	}

	return NULL;
}

static void ui_browser__hists_seek(struct ui_browser *self,
				   off_t offset, int whence)
{
	struct hist_entry *h;
	struct rb_node *nd;
	bool first = true;

	if (self->nr_entries == 0)
		return;

	switch (whence) {
	case SEEK_SET:
		nd = hists__filter_entries(rb_first(self->entries));
		break;
	case SEEK_CUR:
		nd = self->top;
		goto do_offset;
	case SEEK_END:
		nd = hists__filter_prev_entries(rb_last(self->entries));
		first = false;
		break;
	default:
		return;
	}

	/*
	 * Moves not relative to the first visible entry invalidates its
	 * row_offset:
	 */
	h = rb_entry(self->top, struct hist_entry, rb_node);
	h->row_offset = 0;

	/*
	 * Here we have to check if nd is expanded (+), if it is we can't go
	 * the next top level hist_entry, instead we must compute an offset of
	 * what _not_ to show and not change the first visible entry.
	 *
	 * This offset increments when we are going from top to bottom and
	 * decreases when we're going from bottom to top.
	 *
	 * As we don't have backpointers to the top level in the callchains
	 * structure, we need to always print the whole hist_entry callchain,
	 * skipping the first ones that are before the first visible entry
	 * and stop when we printed enough lines to fill the screen.
	 */
do_offset:
	if (offset > 0) {
		do {
			h = rb_entry(nd, struct hist_entry, rb_node);
			if (h->ms.unfolded) {
				u16 remaining = h->nr_rows - h->row_offset;
				if (offset > remaining) {
					offset -= remaining;
					h->row_offset = 0;
				} else {
					h->row_offset += offset;
					offset = 0;
					self->top = nd;
					break;
				}
			}
			nd = hists__filter_entries(rb_next(nd));
			if (nd == NULL)
				break;
			--offset;
			self->top = nd;
		} while (offset != 0);
	} else if (offset < 0) {
		while (1) {
			h = rb_entry(nd, struct hist_entry, rb_node);
			if (h->ms.unfolded) {
				if (first) {
					if (-offset > h->row_offset) {
						offset += h->row_offset;
						h->row_offset = 0;
					} else {
						h->row_offset += offset;
						offset = 0;
						self->top = nd;
						break;
					}
				} else {
					if (-offset > h->nr_rows) {
						offset += h->nr_rows;
						h->row_offset = 0;
					} else {
						h->row_offset = h->nr_rows + offset;
						offset = 0;
						self->top = nd;
						break;
					}
				}
			}

			nd = hists__filter_prev_entries(rb_prev(nd));
			if (nd == NULL)
				break;
			++offset;
			self->top = nd;
			if (offset == 0) {
				/*
				 * Last unfiltered hist_entry, check if it is
				 * unfolded, if it is then we should have
				 * row_offset at its last entry.
				 */
				h = rb_entry(nd, struct hist_entry, rb_node);
				if (h->ms.unfolded)
					h->row_offset = h->nr_rows;
				break;
			}
			first = false;
		}
	} else {
		self->top = nd;
		h = rb_entry(nd, struct hist_entry, rb_node);
		h->row_offset = 0;
	}
}

static struct hist_browser *hist_browser__new(struct hists *hists)
{
	struct hist_browser *self = zalloc(sizeof(*self));

	if (self) {
		self->hists = hists;
		self->b.refresh = hist_browser__refresh;
		self->b.seek = ui_browser__hists_seek;
		self->has_symbols = sort_sym.list.next != NULL;
	}

	return self;
}

static void hist_browser__delete(struct hist_browser *self)
{
	free(self);
}

static struct hist_entry *hist_browser__selected_entry(struct hist_browser *self)
{
	return self->he_selection;
}

static struct thread *hist_browser__selected_thread(struct hist_browser *self)
{
	return self->he_selection->thread;
}

static int hists__browser_title(struct hists *self, char *bf, size_t size,
				const char *ev_name, const struct dso *dso,
				const struct thread *thread)
{
	char unit;
	int printed;
	unsigned long nr_events = self->stats.nr_events[PERF_RECORD_SAMPLE];

	nr_events = convert_unit(nr_events, &unit);
	printed = snprintf(bf, size, "Events: %lu%c %s", nr_events, unit, ev_name);

	if (thread)
		printed += snprintf(bf + printed, size - printed,
				    ", Thread: %s(%d)",
				    (thread->comm_set ? thread->comm : ""),
				    thread->pid);
	if (dso)
		printed += snprintf(bf + printed, size - printed,
				    ", DSO: %s", dso->short_name);
	return printed;
}

static int perf_evsel__hists_browse(struct perf_evsel *evsel, int nr_events,
				    const char *helpline, const char *ev_name,
				    bool left_exits,
				    void(*timer)(void *arg), void *arg,
				    int delay_secs)
{
	struct hists *self = &evsel->hists;
	struct hist_browser *browser = hist_browser__new(self);
	struct pstack *fstack;
	int key = -1;

	if (browser == NULL)
		return -1;

	fstack = pstack__new(2);
	if (fstack == NULL)
		goto out;

	ui_helpline__push(helpline);

	while (1) {
		const struct thread *thread = NULL;
		const struct dso *dso = NULL;
		char *options[16];
		int nr_options = 0, choice = 0, i,
		    annotate = -2, zoom_dso = -2, zoom_thread = -2,
		    browse_map = -2;

		key = hist_browser__run(browser, ev_name, timer, arg, delay_secs);

		if (browser->he_selection != NULL) {
			thread = hist_browser__selected_thread(browser);
			dso = browser->selection->map ? browser->selection->map->dso : NULL;
		}

		switch (key) {
		case NEWT_KEY_TAB:
		case NEWT_KEY_UNTAB:
			/*
			 * Exit the browser, let hists__browser_tree
			 * go to the next or previous
			 */
			goto out_free_stack;
		case 'a':
			if (browser->selection == NULL ||
			    browser->selection->sym == NULL ||
			    browser->selection->map->dso->annotate_warned)
				continue;
			goto do_annotate;
		case 'd':
			goto zoom_dso;
		case 't':
			goto zoom_thread;
		case NEWT_KEY_F1:
		case 'h':
		case '?':
			ui__help_window("h/?/F1    Show this window\n"
					"TAB/UNTAB Switch events\n"
					"q/CTRL+C  Exit browser\n\n"
					"For symbolic views (--sort has sym):\n\n"
					"->        Zoom into DSO/Threads & Annotate current symbol\n"
					"<-        Zoom out\n"
					"a         Annotate current symbol\n"
					"C         Collapse all callchains\n"
					"E         Expand all callchains\n"
					"d         Zoom into current DSO\n"
					"t         Zoom into current Thread\n");
			continue;
		case NEWT_KEY_ENTER:
		case NEWT_KEY_RIGHT:
			/* menu */
			break;
		case NEWT_KEY_LEFT: {
			const void *top;

			if (pstack__empty(fstack)) {
				/*
				 * Go back to the perf_evsel_menu__run or other user
				 */
				if (left_exits)
					goto out_free_stack;
				continue;
			}
			top = pstack__pop(fstack);
			if (top == &browser->dso_filter)
				goto zoom_out_dso;
			if (top == &browser->thread_filter)
				goto zoom_out_thread;
			continue;
		}
		case NEWT_KEY_ESCAPE:
			if (!left_exits &&
			    !ui__dialog_yesno("Do you really want to exit?"))
				continue;
			/* Fall thru */
		default:
			goto out_free_stack;
		}

		if (!browser->has_symbols)
			goto add_exit_option;

		if (browser->selection != NULL &&
		    browser->selection->sym != NULL &&
		    !browser->selection->map->dso->annotate_warned &&
		    asprintf(&options[nr_options], "Annotate %s",
			     browser->selection->sym->name) > 0)
			annotate = nr_options++;

		if (thread != NULL &&
		    asprintf(&options[nr_options], "Zoom %s %s(%d) thread",
			     (browser->thread_filter ? "out of" : "into"),
			     (thread->comm_set ? thread->comm : ""),
			     thread->pid) > 0)
			zoom_thread = nr_options++;

		if (dso != NULL &&
		    asprintf(&options[nr_options], "Zoom %s %s DSO",
			     (browser->dso_filter ? "out of" : "into"),
			     (dso->kernel ? "the Kernel" : dso->short_name)) > 0)
			zoom_dso = nr_options++;

		if (browser->selection != NULL &&
		    browser->selection->map != NULL &&
		    asprintf(&options[nr_options], "Browse map details") > 0)
			browse_map = nr_options++;
add_exit_option:
		options[nr_options++] = (char *)"Exit";

		choice = ui__popup_menu(nr_options, options);

		for (i = 0; i < nr_options - 1; ++i)
			free(options[i]);

		if (choice == nr_options - 1)
			break;

		if (choice == -1)
			continue;

		if (choice == annotate) {
			struct hist_entry *he;
do_annotate:
			he = hist_browser__selected_entry(browser);
			if (he == NULL)
				continue;
			/*
			 * Don't let this be freed, say, by hists__decay_entry.
			 */
			he->used = true;
			hist_entry__tui_annotate(he, evsel->idx, nr_events,
						 timer, arg, delay_secs);
			he->used = false;
			ui_browser__update_nr_entries(&browser->b, browser->hists->nr_entries);
		} else if (choice == browse_map)
			map__browse(browser->selection->map);
		else if (choice == zoom_dso) {
zoom_dso:
			if (browser->dso_filter) {
				pstack__remove(fstack, &browser->dso_filter);
zoom_out_dso:
				ui_helpline__pop();
				browser->dso_filter = NULL;
			} else {
				if (dso == NULL)
					continue;
				ui_helpline__fpush("To zoom out press <- or -> + \"Zoom out of %s DSO\"",
						   dso->kernel ? "the Kernel" : dso->short_name);
				browser->dso_filter = dso;
				pstack__push(fstack, &browser->dso_filter);
			}
			hists__filter_by_dso(self, browser->dso_filter);
			hist_browser__reset(browser);
		} else if (choice == zoom_thread) {
zoom_thread:
			if (browser->thread_filter) {
				pstack__remove(fstack, &browser->thread_filter);
zoom_out_thread:
				ui_helpline__pop();
				browser->thread_filter = NULL;
			} else {
				ui_helpline__fpush("To zoom out press <- or -> + \"Zoom out of %s(%d) thread\"",
						   thread->comm_set ? thread->comm : "",
						   thread->pid);
				browser->thread_filter = thread;
				pstack__push(fstack, &browser->thread_filter);
			}
			hists__filter_by_thread(self, browser->thread_filter);
			hist_browser__reset(browser);
		}
	}
out_free_stack:
	pstack__delete(fstack);
out:
	hist_browser__delete(browser);
	return key;
}

struct perf_evsel_menu {
	struct ui_browser b;
	struct perf_evsel *selection;
};

static void perf_evsel_menu__write(struct ui_browser *browser,
				   void *entry, int row)
{
	struct perf_evsel_menu *menu = container_of(browser,
						    struct perf_evsel_menu, b);
	struct perf_evsel *evsel = list_entry(entry, struct perf_evsel, node);
	bool current_entry = ui_browser__is_current_entry(browser, row);
	unsigned long nr_events = evsel->hists.stats.nr_events[PERF_RECORD_SAMPLE];
	const char *ev_name = event_name(evsel);
	char bf[256], unit;

	ui_browser__set_color(browser, current_entry ? HE_COLORSET_SELECTED :
						       HE_COLORSET_NORMAL);

	nr_events = convert_unit(nr_events, &unit);
	snprintf(bf, sizeof(bf), "%lu%c%s%s", nr_events,
		 unit, unit == ' ' ? "" : " ", ev_name);
	slsmg_write_nstring(bf, browser->width);

	if (current_entry)
		menu->selection = evsel;
}

static int perf_evsel_menu__run(struct perf_evsel_menu *menu,
				int nr_events, const char *help,
				void(*timer)(void *arg), void *arg, int delay_secs)
{
	int exit_keys[] = { NEWT_KEY_ENTER, NEWT_KEY_RIGHT, 0, };
	int delay_msecs = delay_secs * 1000;
	struct perf_evlist *evlist = menu->b.priv;
	struct perf_evsel *pos;
	const char *ev_name, *title = "Available samples";
	int key;

	if (ui_browser__show(&menu->b, title,
			     "ESC: exit, ENTER|->: Browse histograms") < 0)
		return -1;

	if (timer != NULL)
		newtFormSetTimer(menu->b.form, delay_msecs);

	ui_browser__add_exit_keys(&menu->b, exit_keys);

	while (1) {
		key = ui_browser__run(&menu->b);

		switch (key) {
		case -1:
			/* FIXME we need to check if it was es.reason == NEWT_EXIT_TIMER */
			timer(arg);
			continue;
		case NEWT_KEY_RIGHT:
		case NEWT_KEY_ENTER:
			if (!menu->selection)
				continue;
			pos = menu->selection;
			perf_evlist__set_selected(evlist, pos);
browse_hists:
			ev_name = event_name(pos);
			key = perf_evsel__hists_browse(pos, nr_events, help,
						       ev_name, true, timer,
						       arg, delay_secs);
			ui_browser__show_title(&menu->b, title);
			break;
		case NEWT_KEY_LEFT:
			continue;
		case NEWT_KEY_ESCAPE:
			if (!ui__dialog_yesno("Do you really want to exit?"))
				continue;
			/* Fall thru */
		default:
			goto out;
		}

		switch (key) {
		case NEWT_KEY_TAB:
			if (pos->node.next == &evlist->entries)
				pos = list_entry(evlist->entries.next, struct perf_evsel, node);
			else
				pos = list_entry(pos->node.next, struct perf_evsel, node);
			perf_evlist__set_selected(evlist, pos);
			goto browse_hists;
		case NEWT_KEY_UNTAB:
			if (pos->node.prev == &evlist->entries)
				pos = list_entry(evlist->entries.prev, struct perf_evsel, node);
			else
				pos = list_entry(pos->node.prev, struct perf_evsel, node);
			perf_evlist__set_selected(evlist, pos);
			goto browse_hists;
		case 'q':
		case CTRL('c'):
			goto out;
		default:
			break;
		}
	}

out:
	ui_browser__hide(&menu->b);
	return key;
}

static int __perf_evlist__tui_browse_hists(struct perf_evlist *evlist,
					   const char *help,
					   void(*timer)(void *arg), void *arg,
					   int delay_secs)
{
	struct perf_evsel *pos;
	struct perf_evsel_menu menu = {
		.b = {
			.entries    = &evlist->entries,
			.refresh    = ui_browser__list_head_refresh,
			.seek	    = ui_browser__list_head_seek,
			.write	    = perf_evsel_menu__write,
			.nr_entries = evlist->nr_entries,
			.priv	    = evlist,
		},
	};

	ui_helpline__push("Press ESC to exit");

	list_for_each_entry(pos, &evlist->entries, node) {
		const char *ev_name = event_name(pos);
		size_t line_len = strlen(ev_name) + 7;

		if (menu.b.width < line_len)
			menu.b.width = line_len;
		/*
		 * Cache the evsel name, tracepoints have a _high_ cost per
		 * event_name() call.
		 */
		if (pos->name == NULL)
			pos->name = strdup(ev_name);
	}

	return perf_evsel_menu__run(&menu, evlist->nr_entries, help, timer,
				    arg, delay_secs);
}

int perf_evlist__tui_browse_hists(struct perf_evlist *evlist, const char *help,
				  void(*timer)(void *arg), void *arg,
				  int delay_secs)
{

	if (evlist->nr_entries == 1) {
		struct perf_evsel *first = list_entry(evlist->entries.next,
						      struct perf_evsel, node);
		const char *ev_name = event_name(first);
		return perf_evsel__hists_browse(first, evlist->nr_entries, help,
						ev_name, false, timer, arg,
						delay_secs);
	}

	return __perf_evlist__tui_browse_hists(evlist, help,
					       timer, arg, delay_secs);
}
