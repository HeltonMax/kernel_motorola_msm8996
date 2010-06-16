/*
 * security/tomoyo/gc.c
 *
 * Implementation of the Domain-Based Mandatory Access Control.
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 *
 */

#include "common.h"
#include <linux/kthread.h>
#include <linux/slab.h>

enum tomoyo_policy_id {
	TOMOYO_ID_PATH_GROUP,
	TOMOYO_ID_PATH_GROUP_MEMBER,
	TOMOYO_ID_NUMBER_GROUP,
	TOMOYO_ID_NUMBER_GROUP_MEMBER,
	TOMOYO_ID_DOMAIN_INITIALIZER,
	TOMOYO_ID_DOMAIN_KEEPER,
	TOMOYO_ID_AGGREGATOR,
	TOMOYO_ID_ALIAS,
	TOMOYO_ID_GLOBALLY_READABLE,
	TOMOYO_ID_PATTERN,
	TOMOYO_ID_NO_REWRITE,
	TOMOYO_ID_MANAGER,
	TOMOYO_ID_NAME,
	TOMOYO_ID_ACL,
	TOMOYO_ID_DOMAIN,
	TOMOYO_MAX_POLICY
};

struct tomoyo_gc_entry {
	struct list_head list;
	int type;
	void *element;
};
static LIST_HEAD(tomoyo_gc_queue);
static DEFINE_MUTEX(tomoyo_gc_mutex);

/* Caller holds tomoyo_policy_lock mutex. */
static bool tomoyo_add_to_gc(const int type, void *element)
{
	struct tomoyo_gc_entry *entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return false;
	entry->type = type;
	entry->element = element;
	list_add(&entry->list, &tomoyo_gc_queue);
	return true;
}

static void tomoyo_del_allow_read
(struct tomoyo_globally_readable_file_entry *ptr)
{
	tomoyo_put_name(ptr->filename);
}

static void tomoyo_del_file_pattern(struct tomoyo_pattern_entry *ptr)
{
	tomoyo_put_name(ptr->pattern);
}

static void tomoyo_del_no_rewrite(struct tomoyo_no_rewrite_entry *ptr)
{
	tomoyo_put_name(ptr->pattern);
}

static void tomoyo_del_domain_initializer
(struct tomoyo_domain_initializer_entry *ptr)
{
	tomoyo_put_name(ptr->domainname);
	tomoyo_put_name(ptr->program);
}

static void tomoyo_del_domain_keeper(struct tomoyo_domain_keeper_entry *ptr)
{
	tomoyo_put_name(ptr->domainname);
	tomoyo_put_name(ptr->program);
}

static void tomoyo_del_aggregator(struct tomoyo_aggregator_entry *ptr)
{
	tomoyo_put_name(ptr->original_name);
	tomoyo_put_name(ptr->aggregated_name);
}

static void tomoyo_del_alias(struct tomoyo_alias_entry *ptr)
{
	tomoyo_put_name(ptr->original_name);
	tomoyo_put_name(ptr->aliased_name);
}

static void tomoyo_del_manager(struct tomoyo_policy_manager_entry *ptr)
{
	tomoyo_put_name(ptr->manager);
}

static void tomoyo_del_acl(struct tomoyo_acl_info *acl)
{
	switch (acl->type) {
	case TOMOYO_TYPE_PATH_ACL:
		{
			struct tomoyo_path_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->name);
		}
		break;
	case TOMOYO_TYPE_PATH2_ACL:
		{
			struct tomoyo_path2_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->name1);
			tomoyo_put_name_union(&entry->name2);
		}
		break;
	case TOMOYO_TYPE_PATH_NUMBER_ACL:
		{
			struct tomoyo_path_number_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->name);
			tomoyo_put_number_union(&entry->number);
		}
		break;
	case TOMOYO_TYPE_MKDEV_ACL:
		{
			struct tomoyo_mkdev_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->name);
			tomoyo_put_number_union(&entry->mode);
			tomoyo_put_number_union(&entry->major);
			tomoyo_put_number_union(&entry->minor);
		}
		break;
	case TOMOYO_TYPE_MOUNT_ACL:
		{
			struct tomoyo_mount_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->dev_name);
			tomoyo_put_name_union(&entry->dir_name);
			tomoyo_put_name_union(&entry->fs_type);
			tomoyo_put_number_union(&entry->flags);
		}
		break;
	default:
		printk(KERN_WARNING "Unknown type\n");
		break;
	}
}

static bool tomoyo_del_domain(struct tomoyo_domain_info *domain)
{
	struct tomoyo_acl_info *acl;
	struct tomoyo_acl_info *tmp;
	/*
	 * Since we don't protect whole execve() operation using SRCU,
	 * we need to recheck domain->users at this point.
	 *
	 * (1) Reader starts SRCU section upon execve().
	 * (2) Reader traverses tomoyo_domain_list and finds this domain.
	 * (3) Writer marks this domain as deleted.
	 * (4) Garbage collector removes this domain from tomoyo_domain_list
	 *     because this domain is marked as deleted and used by nobody.
	 * (5) Reader saves reference to this domain into
	 *     "struct linux_binprm"->cred->security .
	 * (6) Reader finishes SRCU section, although execve() operation has
	 *     not finished yet.
	 * (7) Garbage collector waits for SRCU synchronization.
	 * (8) Garbage collector kfree() this domain because this domain is
	 *     used by nobody.
	 * (9) Reader finishes execve() operation and restores this domain from
	 *     "struct linux_binprm"->cred->security.
	 *
	 * By updating domain->users at (5), we can solve this race problem
	 * by rechecking domain->users at (8).
	 */
	if (atomic_read(&domain->users))
		return false;
	list_for_each_entry_safe(acl, tmp, &domain->acl_info_list, list) {
		tomoyo_del_acl(acl);
		tomoyo_memory_free(acl);
	}
	tomoyo_put_name(domain->domainname);
	return true;
}


static void tomoyo_del_name(const struct tomoyo_name_entry *ptr)
{
}

static void tomoyo_del_path_group_member(struct tomoyo_path_group_member
					 *member)
{
	tomoyo_put_name(member->member_name);
}

static void tomoyo_del_path_group(struct tomoyo_path_group *group)
{
	tomoyo_put_name(group->group_name);
}

static void tomoyo_del_number_group_member(struct tomoyo_number_group_member
					   *member)
{
}

static void tomoyo_del_number_group(struct tomoyo_number_group *group)
{
	tomoyo_put_name(group->group_name);
}

static struct list_head *tomoyo_policy_list[TOMOYO_MAX_POLICY] = {
	[TOMOYO_ID_GLOBALLY_READABLE] = &tomoyo_globally_readable_list,
	[TOMOYO_ID_PATTERN] = &tomoyo_pattern_list,
	[TOMOYO_ID_NO_REWRITE] = &tomoyo_no_rewrite_list,
	[TOMOYO_ID_DOMAIN_INITIALIZER] = &tomoyo_domain_initializer_list,
	[TOMOYO_ID_DOMAIN_KEEPER] = &tomoyo_domain_keeper_list,
	[TOMOYO_ID_AGGREGATOR] = &tomoyo_aggregator_list,
	[TOMOYO_ID_ALIAS] = &tomoyo_alias_list,
	[TOMOYO_ID_MANAGER] = &tomoyo_policy_manager_list,
};

static bool tomoyo_collect_member(struct list_head *member_list, int id)
{
	struct tomoyo_acl_head *member;
	list_for_each_entry(member, member_list, list) {
		if (!member->is_deleted)
			continue;
		if (!tomoyo_add_to_gc(id, &member->list))
			return false;
		list_del_rcu(&member->list);
	}
        return true;
}

static bool tomoyo_collect_acl(struct tomoyo_domain_info *domain)
{
	struct tomoyo_acl_info *acl;
	list_for_each_entry(acl, &domain->acl_info_list, list) {
		if (!acl->is_deleted)
			continue;
		if (!tomoyo_add_to_gc(TOMOYO_ID_ACL, &acl->list))
			return false;
		list_del_rcu(&acl->list);
	}
	return true;
}

static void tomoyo_collect_entry(void)
{
	int i;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		return;
	for (i = 0; i < TOMOYO_MAX_POLICY; i++) {
		if (tomoyo_policy_list[i])
			if (!tomoyo_collect_member(tomoyo_policy_list[i], i))
				goto unlock;
	}
	{
		struct tomoyo_domain_info *domain;
		list_for_each_entry_rcu(domain, &tomoyo_domain_list, list) {
			if (!tomoyo_collect_acl(domain))
				goto unlock;
			if (!domain->is_deleted || atomic_read(&domain->users))
				continue;
			/*
			 * Nobody is referring this domain. But somebody may
			 * refer this domain after successful execve().
			 * We recheck domain->users after SRCU synchronization.
			 */
			if (tomoyo_add_to_gc(TOMOYO_ID_DOMAIN, domain))
				list_del_rcu(&domain->list);
			else
				goto unlock;
		}
	}
	for (i = 0; i < TOMOYO_MAX_HASH; i++) {
		struct tomoyo_name_entry *ptr;
		list_for_each_entry_rcu(ptr, &tomoyo_name_list[i], list) {
			if (atomic_read(&ptr->users))
				continue;
			if (tomoyo_add_to_gc(TOMOYO_ID_NAME, ptr))
				list_del_rcu(&ptr->list);
			else
				goto unlock;
		}
	}
	{
		struct tomoyo_path_group *group;
		list_for_each_entry_rcu(group, &tomoyo_path_group_list, list) {
			tomoyo_collect_member(&group->member_list,
					      TOMOYO_ID_PATH_GROUP_MEMBER);
			if (!list_empty(&group->member_list) ||
			    atomic_read(&group->users))
				continue;
			if (tomoyo_add_to_gc(TOMOYO_ID_PATH_GROUP, group))
				list_del_rcu(&group->list);
			else
				goto unlock;
		}
	}
	{
		struct tomoyo_number_group *group;
		list_for_each_entry_rcu(group, &tomoyo_number_group_list,
					list) {
			tomoyo_collect_member(&group->member_list,
					      TOMOYO_ID_NUMBER_GROUP_MEMBER);
			if (!list_empty(&group->member_list) ||
			    atomic_read(&group->users))
				continue;
			if (tomoyo_add_to_gc(TOMOYO_ID_NUMBER_GROUP, group))
				list_del_rcu(&group->list);
			else
				goto unlock;
		}
	}
 unlock:
	mutex_unlock(&tomoyo_policy_lock);
}

static void tomoyo_kfree_entry(void)
{
	struct tomoyo_gc_entry *p;
	struct tomoyo_gc_entry *tmp;

	list_for_each_entry_safe(p, tmp, &tomoyo_gc_queue, list) {
		switch (p->type) {
		case TOMOYO_ID_DOMAIN_INITIALIZER:
			tomoyo_del_domain_initializer(p->element);
			break;
		case TOMOYO_ID_DOMAIN_KEEPER:
			tomoyo_del_domain_keeper(p->element);
			break;
		case TOMOYO_ID_AGGREGATOR:
			tomoyo_del_aggregator(p->element);
			break;
		case TOMOYO_ID_ALIAS:
			tomoyo_del_alias(p->element);
			break;
		case TOMOYO_ID_GLOBALLY_READABLE:
			tomoyo_del_allow_read(p->element);
			break;
		case TOMOYO_ID_PATTERN:
			tomoyo_del_file_pattern(p->element);
			break;
		case TOMOYO_ID_NO_REWRITE:
			tomoyo_del_no_rewrite(p->element);
			break;
		case TOMOYO_ID_MANAGER:
			tomoyo_del_manager(p->element);
			break;
		case TOMOYO_ID_NAME:
			tomoyo_del_name(p->element);
			break;
		case TOMOYO_ID_ACL:
			tomoyo_del_acl(p->element);
			break;
		case TOMOYO_ID_DOMAIN:
			if (!tomoyo_del_domain(p->element))
				continue;
			break;
		case TOMOYO_ID_PATH_GROUP_MEMBER:
			tomoyo_del_path_group_member(p->element);
			break;
		case TOMOYO_ID_PATH_GROUP:
			tomoyo_del_path_group(p->element);
			break;
		case TOMOYO_ID_NUMBER_GROUP_MEMBER:
			tomoyo_del_number_group_member(p->element);
			break;
		case TOMOYO_ID_NUMBER_GROUP:
			tomoyo_del_number_group(p->element);
			break;
		default:
			printk(KERN_WARNING "Unknown type\n");
			break;
		}
		tomoyo_memory_free(p->element);
		list_del(&p->list);
		kfree(p);
	}
}

static int tomoyo_gc_thread(void *unused)
{
	daemonize("GC for TOMOYO");
	if (mutex_trylock(&tomoyo_gc_mutex)) {
		int i;
		for (i = 0; i < 10; i++) {
			tomoyo_collect_entry();
			if (list_empty(&tomoyo_gc_queue))
				break;
			synchronize_srcu(&tomoyo_ss);
			tomoyo_kfree_entry();
		}
		mutex_unlock(&tomoyo_gc_mutex);
	}
	do_exit(0);
}

void tomoyo_run_gc(void)
{
	struct task_struct *task = kthread_create(tomoyo_gc_thread, NULL,
						  "GC for TOMOYO");
	if (!IS_ERR(task))
		wake_up_process(task);
}
