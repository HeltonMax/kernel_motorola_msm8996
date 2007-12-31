/*
 * linux/net/sunrpc/svcsock.c
 *
 * These are the RPC server socket internals.
 *
 * The server scheduling algorithm does not always distribute the load
 * evenly when servicing a single client. May need to modify the
 * svc_xprt_enqueue procedure...
 *
 * TCP support is largely untested and may be a little slow. The problem
 * is that we currently do two separate recvfrom's, one for the 4-byte
 * record length, and the second for the actual record. This could possibly
 * be improved by always reading a minimum size of around 100 bytes and
 * tucking any superfluous bytes away in a temporary store. Still, that
 * leaves write requests out in the rain. An alternative may be to peek at
 * the first skb in the queue, and if it matches the next TCP sequence
 * number, to extract the record marker. Yuck.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <net/sock.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/tcp_states.h>
#include <asm/uaccess.h>
#include <asm/ioctls.h>

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/stats.h>

/* SMP locking strategy:
 *
 *	svc_pool->sp_lock protects most of the fields of that pool.
 * 	svc_serv->sv_lock protects sv_tempsocks, sv_permsocks, sv_tmpcnt.
 *	when both need to be taken (rare), svc_serv->sv_lock is first.
 *	BKL protects svc_serv->sv_nrthread.
 *	svc_sock->sk_lock protects the svc_sock->sk_deferred list
 *             and the ->sk_info_authunix cache.
 *	svc_sock->sk_xprt.xpt_flags.XPT_BUSY prevents a svc_sock being
 *	enqueued multiply.
 *
 *	Some flags can be set to certain values at any time
 *	providing that certain rules are followed:
 *
 *	XPT_CONN, XPT_DATA, can be set or cleared at any time.
 *		after a set, svc_xprt_enqueue must be called.
 *		after a clear, the socket must be read/accepted
 *		 if this succeeds, it must be set again.
 *	XPT_CLOSE can set at any time. It is never cleared.
 *      xpt_ref contains a bias of '1' until XPT_DEAD is set.
 *             so when xprt_ref hits zero, we know the transport is dead
 *             and no-one is using it.
 *      XPT_DEAD can only be set while XPT_BUSY is held which ensures
 *             no other thread will be using the socket or will try to
 *	       set XPT_DEAD.
 *
 */

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT


static struct svc_sock *svc_setup_socket(struct svc_serv *, struct socket *,
					 int *errp, int flags);
static void		svc_delete_xprt(struct svc_xprt *xprt);
static void		svc_udp_data_ready(struct sock *, int);
static int		svc_udp_recvfrom(struct svc_rqst *);
static int		svc_udp_sendto(struct svc_rqst *);
static void		svc_close_xprt(struct svc_xprt *xprt);
static void		svc_sock_detach(struct svc_xprt *);
static void		svc_sock_free(struct svc_xprt *);

static struct svc_deferred_req *svc_deferred_dequeue(struct svc_xprt *xprt);
static int svc_deferred_recv(struct svc_rqst *rqstp);
static struct cache_deferred_req *svc_defer(struct cache_req *req);
static struct svc_xprt *svc_create_socket(struct svc_serv *, int,
					  struct sockaddr *, int, int);
static void svc_age_temp_xprts(unsigned long closure);

/* apparently the "standard" is that clients close
 * idle connections after 5 minutes, servers after
 * 6 minutes
 *   http://www.connectathon.org/talks96/nfstcp.pdf
 */
static int svc_conn_age_period = 6*60;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key svc_key[2];
static struct lock_class_key svc_slock_key[2];

static inline void svc_reclassify_socket(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(sock_owned_by_user(sk));
	switch (sk->sk_family) {
	case AF_INET:
		sock_lock_init_class_and_name(sk, "slock-AF_INET-NFSD",
					      &svc_slock_key[0],
					      "sk_xprt.xpt_lock-AF_INET-NFSD",
					      &svc_key[0]);
		break;

	case AF_INET6:
		sock_lock_init_class_and_name(sk, "slock-AF_INET6-NFSD",
					      &svc_slock_key[1],
					      "sk_xprt.xpt_lock-AF_INET6-NFSD",
					      &svc_key[1]);
		break;

	default:
		BUG();
	}
}
#else
static inline void svc_reclassify_socket(struct socket *sock)
{
}
#endif

static char *__svc_print_addr(struct sockaddr *addr, char *buf, size_t len)
{
	switch (addr->sa_family) {
	case AF_INET:
		snprintf(buf, len, "%u.%u.%u.%u, port=%u",
			NIPQUAD(((struct sockaddr_in *) addr)->sin_addr),
			ntohs(((struct sockaddr_in *) addr)->sin_port));
		break;

	case AF_INET6:
		snprintf(buf, len, "%x:%x:%x:%x:%x:%x:%x:%x, port=%u",
			NIP6(((struct sockaddr_in6 *) addr)->sin6_addr),
			ntohs(((struct sockaddr_in6 *) addr)->sin6_port));
		break;

	default:
		snprintf(buf, len, "unknown address type: %d", addr->sa_family);
		break;
	}
	return buf;
}

/**
 * svc_print_addr - Format rq_addr field for printing
 * @rqstp: svc_rqst struct containing address to print
 * @buf: target buffer for formatted address
 * @len: length of target buffer
 *
 */
char *svc_print_addr(struct svc_rqst *rqstp, char *buf, size_t len)
{
	return __svc_print_addr(svc_addr(rqstp), buf, len);
}
EXPORT_SYMBOL_GPL(svc_print_addr);

/*
 * Queue up an idle server thread.  Must have pool->sp_lock held.
 * Note: this is really a stack rather than a queue, so that we only
 * use as many different threads as we need, and the rest don't pollute
 * the cache.
 */
static inline void
svc_thread_enqueue(struct svc_pool *pool, struct svc_rqst *rqstp)
{
	list_add(&rqstp->rq_list, &pool->sp_threads);
}

/*
 * Dequeue an nfsd thread.  Must have pool->sp_lock held.
 */
static inline void
svc_thread_dequeue(struct svc_pool *pool, struct svc_rqst *rqstp)
{
	list_del(&rqstp->rq_list);
}

/*
 * Release an skbuff after use
 */
static void svc_release_skb(struct svc_rqst *rqstp)
{
	struct sk_buff *skb = rqstp->rq_xprt_ctxt;
	struct svc_deferred_req *dr = rqstp->rq_deferred;

	if (skb) {
		rqstp->rq_xprt_ctxt = NULL;

		dprintk("svc: service %p, releasing skb %p\n", rqstp, skb);
		skb_free_datagram(rqstp->rq_sock->sk_sk, skb);
	}
	if (dr) {
		rqstp->rq_deferred = NULL;
		kfree(dr);
	}
}

/*
 * Queue up a socket with data pending. If there are idle nfsd
 * processes, wake 'em up.
 *
 */
void svc_xprt_enqueue(struct svc_xprt *xprt)
{
	struct svc_serv	*serv = xprt->xpt_server;
	struct svc_pool *pool;
	struct svc_rqst	*rqstp;
	int cpu;

	if (!(xprt->xpt_flags &
	      ((1<<XPT_CONN)|(1<<XPT_DATA)|(1<<XPT_CLOSE)|(1<<XPT_DEFERRED))))
		return;
	if (test_bit(XPT_DEAD, &xprt->xpt_flags))
		return;

	cpu = get_cpu();
	pool = svc_pool_for_cpu(xprt->xpt_server, cpu);
	put_cpu();

	spin_lock_bh(&pool->sp_lock);

	if (!list_empty(&pool->sp_threads) &&
	    !list_empty(&pool->sp_sockets))
		printk(KERN_ERR
		       "svc_xprt_enqueue: "
		       "threads and transports both waiting??\n");

	if (test_bit(XPT_DEAD, &xprt->xpt_flags)) {
		/* Don't enqueue dead sockets */
		dprintk("svc: transport %p is dead, not enqueued\n", xprt);
		goto out_unlock;
	}

	/* Mark socket as busy. It will remain in this state until the
	 * server has processed all pending data and put the socket back
	 * on the idle list.  We update XPT_BUSY atomically because
	 * it also guards against trying to enqueue the svc_sock twice.
	 */
	if (test_and_set_bit(XPT_BUSY, &xprt->xpt_flags)) {
		/* Don't enqueue socket while already enqueued */
		dprintk("svc: transport %p busy, not enqueued\n", xprt);
		goto out_unlock;
	}
	BUG_ON(xprt->xpt_pool != NULL);
	xprt->xpt_pool = pool;

	/* Handle pending connection */
	if (test_bit(XPT_CONN, &xprt->xpt_flags))
		goto process;

	/* Handle close in-progress */
	if (test_bit(XPT_CLOSE, &xprt->xpt_flags))
		goto process;

	/* Check if we have space to reply to a request */
	if (!xprt->xpt_ops->xpo_has_wspace(xprt)) {
		/* Don't enqueue while not enough space for reply */
		dprintk("svc: no write space, transport %p  not enqueued\n",
			xprt);
		xprt->xpt_pool = NULL;
		clear_bit(XPT_BUSY, &xprt->xpt_flags);
		goto out_unlock;
	}

 process:
	if (!list_empty(&pool->sp_threads)) {
		rqstp = list_entry(pool->sp_threads.next,
				   struct svc_rqst,
				   rq_list);
		dprintk("svc: transport %p served by daemon %p\n",
			xprt, rqstp);
		svc_thread_dequeue(pool, rqstp);
		if (rqstp->rq_xprt)
			printk(KERN_ERR
				"svc_xprt_enqueue: server %p, rq_xprt=%p!\n",
				rqstp, rqstp->rq_xprt);
		rqstp->rq_xprt = xprt;
		svc_xprt_get(xprt);
		rqstp->rq_reserved = serv->sv_max_mesg;
		atomic_add(rqstp->rq_reserved, &xprt->xpt_reserved);
		BUG_ON(xprt->xpt_pool != pool);
		wake_up(&rqstp->rq_wait);
	} else {
		dprintk("svc: transport %p put into queue\n", xprt);
		list_add_tail(&xprt->xpt_ready, &pool->sp_sockets);
		BUG_ON(xprt->xpt_pool != pool);
	}

out_unlock:
	spin_unlock_bh(&pool->sp_lock);
}
EXPORT_SYMBOL_GPL(svc_xprt_enqueue);

/*
 * Dequeue the first socket.  Must be called with the pool->sp_lock held.
 */
static struct svc_xprt *svc_xprt_dequeue(struct svc_pool *pool)
{
	struct svc_xprt	*xprt;

	if (list_empty(&pool->sp_sockets))
		return NULL;

	xprt = list_entry(pool->sp_sockets.next,
			  struct svc_xprt, xpt_ready);
	list_del_init(&xprt->xpt_ready);

	dprintk("svc: transport %p dequeued, inuse=%d\n",
		xprt, atomic_read(&xprt->xpt_ref.refcount));

	return xprt;
}

/*
 * svc_xprt_received conditionally queues the transport for processing
 * by another thread. The caller must hold the XPT_BUSY bit and must
 * not thereafter touch transport data.
 *
 * Note: XPT_DATA only gets cleared when a read-attempt finds no (or
 * insufficient) data.
 */
void svc_xprt_received(struct svc_xprt *xprt)
{
	BUG_ON(!test_bit(XPT_BUSY, &xprt->xpt_flags));
	xprt->xpt_pool = NULL;
	clear_bit(XPT_BUSY, &xprt->xpt_flags);
	svc_xprt_enqueue(xprt);
}
EXPORT_SYMBOL_GPL(svc_xprt_received);

/**
 * svc_reserve - change the space reserved for the reply to a request.
 * @rqstp:  The request in question
 * @space: new max space to reserve
 *
 * Each request reserves some space on the output queue of the socket
 * to make sure the reply fits.  This function reduces that reserved
 * space to be the amount of space used already, plus @space.
 *
 */
void svc_reserve(struct svc_rqst *rqstp, int space)
{
	space += rqstp->rq_res.head[0].iov_len;

	if (space < rqstp->rq_reserved) {
		struct svc_xprt *xprt = rqstp->rq_xprt;
		atomic_sub((rqstp->rq_reserved - space), &xprt->xpt_reserved);
		rqstp->rq_reserved = space;

		svc_xprt_enqueue(xprt);
	}
}

static void svc_xprt_release(struct svc_rqst *rqstp)
{
	struct svc_xprt	*xprt = rqstp->rq_xprt;

	rqstp->rq_xprt->xpt_ops->xpo_release_rqst(rqstp);

	svc_free_res_pages(rqstp);
	rqstp->rq_res.page_len = 0;
	rqstp->rq_res.page_base = 0;

	/* Reset response buffer and release
	 * the reservation.
	 * But first, check that enough space was reserved
	 * for the reply, otherwise we have a bug!
	 */
	if ((rqstp->rq_res.len) >  rqstp->rq_reserved)
		printk(KERN_ERR "RPC request reserved %d but used %d\n",
		       rqstp->rq_reserved,
		       rqstp->rq_res.len);

	rqstp->rq_res.head[0].iov_len = 0;
	svc_reserve(rqstp, 0);
	rqstp->rq_xprt = NULL;

	svc_xprt_put(xprt);
}

/*
 * External function to wake up a server waiting for data
 * This really only makes sense for services like lockd
 * which have exactly one thread anyway.
 */
void
svc_wake_up(struct svc_serv *serv)
{
	struct svc_rqst	*rqstp;
	unsigned int i;
	struct svc_pool *pool;

	for (i = 0; i < serv->sv_nrpools; i++) {
		pool = &serv->sv_pools[i];

		spin_lock_bh(&pool->sp_lock);
		if (!list_empty(&pool->sp_threads)) {
			rqstp = list_entry(pool->sp_threads.next,
					   struct svc_rqst,
					   rq_list);
			dprintk("svc: daemon %p woken up.\n", rqstp);
			/*
			svc_thread_dequeue(pool, rqstp);
			rqstp->rq_sock = NULL;
			 */
			wake_up(&rqstp->rq_wait);
		}
		spin_unlock_bh(&pool->sp_lock);
	}
}

union svc_pktinfo_u {
	struct in_pktinfo pkti;
	struct in6_pktinfo pkti6;
};
#define SVC_PKTINFO_SPACE \
	CMSG_SPACE(sizeof(union svc_pktinfo_u))

static void svc_set_cmsg_data(struct svc_rqst *rqstp, struct cmsghdr *cmh)
{
	switch (rqstp->rq_sock->sk_sk->sk_family) {
	case AF_INET: {
			struct in_pktinfo *pki = CMSG_DATA(cmh);

			cmh->cmsg_level = SOL_IP;
			cmh->cmsg_type = IP_PKTINFO;
			pki->ipi_ifindex = 0;
			pki->ipi_spec_dst.s_addr = rqstp->rq_daddr.addr.s_addr;
			cmh->cmsg_len = CMSG_LEN(sizeof(*pki));
		}
		break;

	case AF_INET6: {
			struct in6_pktinfo *pki = CMSG_DATA(cmh);

			cmh->cmsg_level = SOL_IPV6;
			cmh->cmsg_type = IPV6_PKTINFO;
			pki->ipi6_ifindex = 0;
			ipv6_addr_copy(&pki->ipi6_addr,
					&rqstp->rq_daddr.addr6);
			cmh->cmsg_len = CMSG_LEN(sizeof(*pki));
		}
		break;
	}
	return;
}

/*
 * Generic sendto routine
 */
static int
svc_sendto(struct svc_rqst *rqstp, struct xdr_buf *xdr)
{
	struct svc_sock	*svsk = rqstp->rq_sock;
	struct socket	*sock = svsk->sk_sock;
	int		slen;
	union {
		struct cmsghdr	hdr;
		long		all[SVC_PKTINFO_SPACE / sizeof(long)];
	} buffer;
	struct cmsghdr *cmh = &buffer.hdr;
	int		len = 0;
	int		result;
	int		size;
	struct page	**ppage = xdr->pages;
	size_t		base = xdr->page_base;
	unsigned int	pglen = xdr->page_len;
	unsigned int	flags = MSG_MORE;
	char		buf[RPC_MAX_ADDRBUFLEN];

	slen = xdr->len;

	if (rqstp->rq_prot == IPPROTO_UDP) {
		struct msghdr msg = {
			.msg_name	= &rqstp->rq_addr,
			.msg_namelen	= rqstp->rq_addrlen,
			.msg_control	= cmh,
			.msg_controllen	= sizeof(buffer),
			.msg_flags	= MSG_MORE,
		};

		svc_set_cmsg_data(rqstp, cmh);

		if (sock_sendmsg(sock, &msg, 0) < 0)
			goto out;
	}

	/* send head */
	if (slen == xdr->head[0].iov_len)
		flags = 0;
	len = kernel_sendpage(sock, rqstp->rq_respages[0], 0,
				  xdr->head[0].iov_len, flags);
	if (len != xdr->head[0].iov_len)
		goto out;
	slen -= xdr->head[0].iov_len;
	if (slen == 0)
		goto out;

	/* send page data */
	size = PAGE_SIZE - base < pglen ? PAGE_SIZE - base : pglen;
	while (pglen > 0) {
		if (slen == size)
			flags = 0;
		result = kernel_sendpage(sock, *ppage, base, size, flags);
		if (result > 0)
			len += result;
		if (result != size)
			goto out;
		slen -= size;
		pglen -= size;
		size = PAGE_SIZE < pglen ? PAGE_SIZE : pglen;
		base = 0;
		ppage++;
	}
	/* send tail */
	if (xdr->tail[0].iov_len) {
		result = kernel_sendpage(sock, rqstp->rq_respages[0],
					     ((unsigned long)xdr->tail[0].iov_base)
						& (PAGE_SIZE-1),
					     xdr->tail[0].iov_len, 0);

		if (result > 0)
			len += result;
	}
out:
	dprintk("svc: socket %p sendto([%p %Zu... ], %d) = %d (addr %s)\n",
		rqstp->rq_sock, xdr->head[0].iov_base, xdr->head[0].iov_len,
		xdr->len, len, svc_print_addr(rqstp, buf, sizeof(buf)));

	return len;
}

/*
 * Report socket names for nfsdfs
 */
static int one_sock_name(char *buf, struct svc_sock *svsk)
{
	int len;

	switch(svsk->sk_sk->sk_family) {
	case AF_INET:
		len = sprintf(buf, "ipv4 %s %u.%u.%u.%u %d\n",
			      svsk->sk_sk->sk_protocol==IPPROTO_UDP?
			      "udp" : "tcp",
			      NIPQUAD(inet_sk(svsk->sk_sk)->rcv_saddr),
			      inet_sk(svsk->sk_sk)->num);
		break;
	default:
		len = sprintf(buf, "*unknown-%d*\n",
			       svsk->sk_sk->sk_family);
	}
	return len;
}

int
svc_sock_names(char *buf, struct svc_serv *serv, char *toclose)
{
	struct svc_sock *svsk, *closesk = NULL;
	int len = 0;

	if (!serv)
		return 0;
	spin_lock_bh(&serv->sv_lock);
	list_for_each_entry(svsk, &serv->sv_permsocks, sk_xprt.xpt_list) {
		int onelen = one_sock_name(buf+len, svsk);
		if (toclose && strcmp(toclose, buf+len) == 0)
			closesk = svsk;
		else
			len += onelen;
	}
	spin_unlock_bh(&serv->sv_lock);
	if (closesk)
		/* Should unregister with portmap, but you cannot
		 * unregister just one protocol...
		 */
		svc_close_xprt(&closesk->sk_xprt);
	else if (toclose)
		return -ENOENT;
	return len;
}
EXPORT_SYMBOL(svc_sock_names);

/*
 * Check input queue length
 */
static int
svc_recv_available(struct svc_sock *svsk)
{
	struct socket	*sock = svsk->sk_sock;
	int		avail, err;

	err = kernel_sock_ioctl(sock, TIOCINQ, (unsigned long) &avail);

	return (err >= 0)? avail : err;
}

/*
 * Generic recvfrom routine.
 */
static int
svc_recvfrom(struct svc_rqst *rqstp, struct kvec *iov, int nr, int buflen)
{
	struct svc_sock *svsk = rqstp->rq_sock;
	struct msghdr msg = {
		.msg_flags	= MSG_DONTWAIT,
	};
	int len;

	len = kernel_recvmsg(svsk->sk_sock, &msg, iov, nr, buflen,
				msg.msg_flags);

	dprintk("svc: socket %p recvfrom(%p, %Zu) = %d\n",
		svsk, iov[0].iov_base, iov[0].iov_len, len);
	return len;
}

/*
 * Set socket snd and rcv buffer lengths
 */
static inline void
svc_sock_setbufsize(struct socket *sock, unsigned int snd, unsigned int rcv)
{
#if 0
	mm_segment_t	oldfs;
	oldfs = get_fs(); set_fs(KERNEL_DS);
	sock_setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
			(char*)&snd, sizeof(snd));
	sock_setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
			(char*)&rcv, sizeof(rcv));
#else
	/* sock_setsockopt limits use to sysctl_?mem_max,
	 * which isn't acceptable.  Until that is made conditional
	 * on not having CAP_SYS_RESOURCE or similar, we go direct...
	 * DaveM said I could!
	 */
	lock_sock(sock->sk);
	sock->sk->sk_sndbuf = snd * 2;
	sock->sk->sk_rcvbuf = rcv * 2;
	sock->sk->sk_userlocks |= SOCK_SNDBUF_LOCK|SOCK_RCVBUF_LOCK;
	release_sock(sock->sk);
#endif
}
/*
 * INET callback when data has been received on the socket.
 */
static void
svc_udp_data_ready(struct sock *sk, int count)
{
	struct svc_sock	*svsk = (struct svc_sock *)sk->sk_user_data;

	if (svsk) {
		dprintk("svc: socket %p(inet %p), count=%d, busy=%d\n",
			svsk, sk, count,
			test_bit(XPT_BUSY, &svsk->sk_xprt.xpt_flags));
		set_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags);
		svc_xprt_enqueue(&svsk->sk_xprt);
	}
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);
}

/*
 * INET callback when space is newly available on the socket.
 */
static void
svc_write_space(struct sock *sk)
{
	struct svc_sock	*svsk = (struct svc_sock *)(sk->sk_user_data);

	if (svsk) {
		dprintk("svc: socket %p(inet %p), write_space busy=%d\n",
			svsk, sk, test_bit(XPT_BUSY, &svsk->sk_xprt.xpt_flags));
		svc_xprt_enqueue(&svsk->sk_xprt);
	}

	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep)) {
		dprintk("RPC svc_write_space: someone sleeping on %p\n",
		       svsk);
		wake_up_interruptible(sk->sk_sleep);
	}
}

/*
 * Copy the UDP datagram's destination address to the rqstp structure.
 * The 'destination' address in this case is the address to which the
 * peer sent the datagram, i.e. our local address. For multihomed
 * hosts, this can change from msg to msg. Note that only the IP
 * address changes, the port number should remain the same.
 */
static void svc_udp_get_dest_address(struct svc_rqst *rqstp,
				     struct cmsghdr *cmh)
{
	switch (rqstp->rq_sock->sk_sk->sk_family) {
	case AF_INET: {
		struct in_pktinfo *pki = CMSG_DATA(cmh);
		rqstp->rq_daddr.addr.s_addr = pki->ipi_spec_dst.s_addr;
		break;
		}
	case AF_INET6: {
		struct in6_pktinfo *pki = CMSG_DATA(cmh);
		ipv6_addr_copy(&rqstp->rq_daddr.addr6, &pki->ipi6_addr);
		break;
		}
	}
}

/*
 * Receive a datagram from a UDP socket.
 */
static int
svc_udp_recvfrom(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk = rqstp->rq_sock;
	struct svc_serv	*serv = svsk->sk_xprt.xpt_server;
	struct sk_buff	*skb;
	union {
		struct cmsghdr	hdr;
		long		all[SVC_PKTINFO_SPACE / sizeof(long)];
	} buffer;
	struct cmsghdr *cmh = &buffer.hdr;
	int		err, len;
	struct msghdr msg = {
		.msg_name = svc_addr(rqstp),
		.msg_control = cmh,
		.msg_controllen = sizeof(buffer),
		.msg_flags = MSG_DONTWAIT,
	};

	if (test_and_clear_bit(XPT_CHNGBUF, &svsk->sk_xprt.xpt_flags))
	    /* udp sockets need large rcvbuf as all pending
	     * requests are still in that buffer.  sndbuf must
	     * also be large enough that there is enough space
	     * for one reply per thread.  We count all threads
	     * rather than threads in a particular pool, which
	     * provides an upper bound on the number of threads
	     * which will access the socket.
	     */
	    svc_sock_setbufsize(svsk->sk_sock,
				(serv->sv_nrthreads+3) * serv->sv_max_mesg,
				(serv->sv_nrthreads+3) * serv->sv_max_mesg);

	clear_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags);
	skb = NULL;
	err = kernel_recvmsg(svsk->sk_sock, &msg, NULL,
			     0, 0, MSG_PEEK | MSG_DONTWAIT);
	if (err >= 0)
		skb = skb_recv_datagram(svsk->sk_sk, 0, 1, &err);

	if (skb == NULL) {
		if (err != -EAGAIN) {
			/* possibly an icmp error */
			dprintk("svc: recvfrom returned error %d\n", -err);
			set_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags);
		}
		svc_xprt_received(&svsk->sk_xprt);
		return -EAGAIN;
	}
	len = svc_addr_len(svc_addr(rqstp));
	if (len < 0)
		return len;
	rqstp->rq_addrlen = len;
	if (skb->tstamp.tv64 == 0) {
		skb->tstamp = ktime_get_real();
		/* Don't enable netstamp, sunrpc doesn't
		   need that much accuracy */
	}
	svsk->sk_sk->sk_stamp = skb->tstamp;
	set_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags); /* there may be more data... */

	/*
	 * Maybe more packets - kick another thread ASAP.
	 */
	svc_xprt_received(&svsk->sk_xprt);

	len  = skb->len - sizeof(struct udphdr);
	rqstp->rq_arg.len = len;

	rqstp->rq_prot = IPPROTO_UDP;

	if (cmh->cmsg_level != IPPROTO_IP ||
	    cmh->cmsg_type != IP_PKTINFO) {
		if (net_ratelimit())
			printk("rpcsvc: received unknown control message:"
			       "%d/%d\n",
			       cmh->cmsg_level, cmh->cmsg_type);
		skb_free_datagram(svsk->sk_sk, skb);
		return 0;
	}
	svc_udp_get_dest_address(rqstp, cmh);

	if (skb_is_nonlinear(skb)) {
		/* we have to copy */
		local_bh_disable();
		if (csum_partial_copy_to_xdr(&rqstp->rq_arg, skb)) {
			local_bh_enable();
			/* checksum error */
			skb_free_datagram(svsk->sk_sk, skb);
			return 0;
		}
		local_bh_enable();
		skb_free_datagram(svsk->sk_sk, skb);
	} else {
		/* we can use it in-place */
		rqstp->rq_arg.head[0].iov_base = skb->data + sizeof(struct udphdr);
		rqstp->rq_arg.head[0].iov_len = len;
		if (skb_checksum_complete(skb)) {
			skb_free_datagram(svsk->sk_sk, skb);
			return 0;
		}
		rqstp->rq_xprt_ctxt = skb;
	}

	rqstp->rq_arg.page_base = 0;
	if (len <= rqstp->rq_arg.head[0].iov_len) {
		rqstp->rq_arg.head[0].iov_len = len;
		rqstp->rq_arg.page_len = 0;
		rqstp->rq_respages = rqstp->rq_pages+1;
	} else {
		rqstp->rq_arg.page_len = len - rqstp->rq_arg.head[0].iov_len;
		rqstp->rq_respages = rqstp->rq_pages + 1 +
			DIV_ROUND_UP(rqstp->rq_arg.page_len, PAGE_SIZE);
	}

	if (serv->sv_stats)
		serv->sv_stats->netudpcnt++;

	return len;
}

static int
svc_udp_sendto(struct svc_rqst *rqstp)
{
	int		error;

	error = svc_sendto(rqstp, &rqstp->rq_res);
	if (error == -ECONNREFUSED)
		/* ICMP error on earlier request. */
		error = svc_sendto(rqstp, &rqstp->rq_res);

	return error;
}

static void svc_udp_prep_reply_hdr(struct svc_rqst *rqstp)
{
}

static int svc_udp_has_wspace(struct svc_xprt *xprt)
{
	struct svc_sock *svsk = container_of(xprt, struct svc_sock, sk_xprt);
	struct svc_serv	*serv = xprt->xpt_server;
	unsigned long required;

	/*
	 * Set the SOCK_NOSPACE flag before checking the available
	 * sock space.
	 */
	set_bit(SOCK_NOSPACE, &svsk->sk_sock->flags);
	required = atomic_read(&svsk->sk_xprt.xpt_reserved) + serv->sv_max_mesg;
	if (required*2 > sock_wspace(svsk->sk_sk))
		return 0;
	clear_bit(SOCK_NOSPACE, &svsk->sk_sock->flags);
	return 1;
}

static struct svc_xprt *svc_udp_accept(struct svc_xprt *xprt)
{
	BUG();
	return NULL;
}

static struct svc_xprt *svc_udp_create(struct svc_serv *serv,
				       struct sockaddr *sa, int salen,
				       int flags)
{
	return svc_create_socket(serv, IPPROTO_UDP, sa, salen, flags);
}

static struct svc_xprt_ops svc_udp_ops = {
	.xpo_create = svc_udp_create,
	.xpo_recvfrom = svc_udp_recvfrom,
	.xpo_sendto = svc_udp_sendto,
	.xpo_release_rqst = svc_release_skb,
	.xpo_detach = svc_sock_detach,
	.xpo_free = svc_sock_free,
	.xpo_prep_reply_hdr = svc_udp_prep_reply_hdr,
	.xpo_has_wspace = svc_udp_has_wspace,
	.xpo_accept = svc_udp_accept,
};

static struct svc_xprt_class svc_udp_class = {
	.xcl_name = "udp",
	.xcl_owner = THIS_MODULE,
	.xcl_ops = &svc_udp_ops,
	.xcl_max_payload = RPCSVC_MAXPAYLOAD_UDP,
};

static void svc_udp_init(struct svc_sock *svsk, struct svc_serv *serv)
{
	int one = 1;
	mm_segment_t oldfs;

	svc_xprt_init(&svc_udp_class, &svsk->sk_xprt, serv);
	clear_bit(XPT_CACHE_AUTH, &svsk->sk_xprt.xpt_flags);
	svsk->sk_sk->sk_data_ready = svc_udp_data_ready;
	svsk->sk_sk->sk_write_space = svc_write_space;

	/* initialise setting must have enough space to
	 * receive and respond to one request.
	 * svc_udp_recvfrom will re-adjust if necessary
	 */
	svc_sock_setbufsize(svsk->sk_sock,
			    3 * svsk->sk_xprt.xpt_server->sv_max_mesg,
			    3 * svsk->sk_xprt.xpt_server->sv_max_mesg);

	set_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags); /* might have come in before data_ready set up */
	set_bit(XPT_CHNGBUF, &svsk->sk_xprt.xpt_flags);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	/* make sure we get destination address info */
	svsk->sk_sock->ops->setsockopt(svsk->sk_sock, IPPROTO_IP, IP_PKTINFO,
				       (char __user *)&one, sizeof(one));
	set_fs(oldfs);
}

/*
 * A data_ready event on a listening socket means there's a connection
 * pending. Do not use state_change as a substitute for it.
 */
static void
svc_tcp_listen_data_ready(struct sock *sk, int count_unused)
{
	struct svc_sock	*svsk = (struct svc_sock *)sk->sk_user_data;

	dprintk("svc: socket %p TCP (listen) state change %d\n",
		sk, sk->sk_state);

	/*
	 * This callback may called twice when a new connection
	 * is established as a child socket inherits everything
	 * from a parent LISTEN socket.
	 * 1) data_ready method of the parent socket will be called
	 *    when one of child sockets become ESTABLISHED.
	 * 2) data_ready method of the child socket may be called
	 *    when it receives data before the socket is accepted.
	 * In case of 2, we should ignore it silently.
	 */
	if (sk->sk_state == TCP_LISTEN) {
		if (svsk) {
			set_bit(XPT_CONN, &svsk->sk_xprt.xpt_flags);
			svc_xprt_enqueue(&svsk->sk_xprt);
		} else
			printk("svc: socket %p: no user data\n", sk);
	}

	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible_all(sk->sk_sleep);
}

/*
 * A state change on a connected socket means it's dying or dead.
 */
static void
svc_tcp_state_change(struct sock *sk)
{
	struct svc_sock	*svsk = (struct svc_sock *)sk->sk_user_data;

	dprintk("svc: socket %p TCP (connected) state change %d (svsk %p)\n",
		sk, sk->sk_state, sk->sk_user_data);

	if (!svsk)
		printk("svc: socket %p: no user data\n", sk);
	else {
		set_bit(XPT_CLOSE, &svsk->sk_xprt.xpt_flags);
		svc_xprt_enqueue(&svsk->sk_xprt);
	}
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible_all(sk->sk_sleep);
}

static void
svc_tcp_data_ready(struct sock *sk, int count)
{
	struct svc_sock *svsk = (struct svc_sock *)sk->sk_user_data;

	dprintk("svc: socket %p TCP data ready (svsk %p)\n",
		sk, sk->sk_user_data);
	if (svsk) {
		set_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags);
		svc_xprt_enqueue(&svsk->sk_xprt);
	}
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);
}

static inline int svc_port_is_privileged(struct sockaddr *sin)
{
	switch (sin->sa_family) {
	case AF_INET:
		return ntohs(((struct sockaddr_in *)sin)->sin_port)
			< PROT_SOCK;
	case AF_INET6:
		return ntohs(((struct sockaddr_in6 *)sin)->sin6_port)
			< PROT_SOCK;
	default:
		return 0;
	}
}

/*
 * Accept a TCP connection
 */
static struct svc_xprt *svc_tcp_accept(struct svc_xprt *xprt)
{
	struct svc_sock *svsk = container_of(xprt, struct svc_sock, sk_xprt);
	struct sockaddr_storage addr;
	struct sockaddr	*sin = (struct sockaddr *) &addr;
	struct svc_serv	*serv = svsk->sk_xprt.xpt_server;
	struct socket	*sock = svsk->sk_sock;
	struct socket	*newsock;
	struct svc_sock	*newsvsk;
	int		err, slen;
	char		buf[RPC_MAX_ADDRBUFLEN];

	dprintk("svc: tcp_accept %p sock %p\n", svsk, sock);
	if (!sock)
		return NULL;

	clear_bit(XPT_CONN, &svsk->sk_xprt.xpt_flags);
	err = kernel_accept(sock, &newsock, O_NONBLOCK);
	if (err < 0) {
		if (err == -ENOMEM)
			printk(KERN_WARNING "%s: no more sockets!\n",
			       serv->sv_name);
		else if (err != -EAGAIN && net_ratelimit())
			printk(KERN_WARNING "%s: accept failed (err %d)!\n",
				   serv->sv_name, -err);
		return NULL;
	}
	set_bit(XPT_CONN, &svsk->sk_xprt.xpt_flags);

	err = kernel_getpeername(newsock, sin, &slen);
	if (err < 0) {
		if (net_ratelimit())
			printk(KERN_WARNING "%s: peername failed (err %d)!\n",
				   serv->sv_name, -err);
		goto failed;		/* aborted connection or whatever */
	}

	/* Ideally, we would want to reject connections from unauthorized
	 * hosts here, but when we get encryption, the IP of the host won't
	 * tell us anything.  For now just warn about unpriv connections.
	 */
	if (!svc_port_is_privileged(sin)) {
		dprintk(KERN_WARNING
			"%s: connect from unprivileged port: %s\n",
			serv->sv_name,
			__svc_print_addr(sin, buf, sizeof(buf)));
	}
	dprintk("%s: connect from %s\n", serv->sv_name,
		__svc_print_addr(sin, buf, sizeof(buf)));

	/* make sure that a write doesn't block forever when
	 * low on memory
	 */
	newsock->sk->sk_sndtimeo = HZ*30;

	if (!(newsvsk = svc_setup_socket(serv, newsock, &err,
				 (SVC_SOCK_ANONYMOUS | SVC_SOCK_TEMPORARY))))
		goto failed;
	svc_xprt_set_remote(&newsvsk->sk_xprt, sin, slen);
	err = kernel_getsockname(newsock, sin, &slen);
	if (unlikely(err < 0)) {
		dprintk("svc_tcp_accept: kernel_getsockname error %d\n", -err);
		slen = offsetof(struct sockaddr, sa_data);
	}
	svc_xprt_set_local(&newsvsk->sk_xprt, sin, slen);

	if (serv->sv_stats)
		serv->sv_stats->nettcpconn++;

	return &newsvsk->sk_xprt;

failed:
	sock_release(newsock);
	return NULL;
}

/*
 * Receive data from a TCP socket.
 */
static int
svc_tcp_recvfrom(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk = rqstp->rq_sock;
	struct svc_serv	*serv = svsk->sk_xprt.xpt_server;
	int		len;
	struct kvec *vec;
	int pnum, vlen;

	dprintk("svc: tcp_recv %p data %d conn %d close %d\n",
		svsk, test_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags),
		test_bit(XPT_CONN, &svsk->sk_xprt.xpt_flags),
		test_bit(XPT_CLOSE, &svsk->sk_xprt.xpt_flags));

	if (test_and_clear_bit(XPT_CHNGBUF, &svsk->sk_xprt.xpt_flags))
		/* sndbuf needs to have room for one request
		 * per thread, otherwise we can stall even when the
		 * network isn't a bottleneck.
		 *
		 * We count all threads rather than threads in a
		 * particular pool, which provides an upper bound
		 * on the number of threads which will access the socket.
		 *
		 * rcvbuf just needs to be able to hold a few requests.
		 * Normally they will be removed from the queue
		 * as soon a a complete request arrives.
		 */
		svc_sock_setbufsize(svsk->sk_sock,
				    (serv->sv_nrthreads+3) * serv->sv_max_mesg,
				    3 * serv->sv_max_mesg);

	clear_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags);

	/* Receive data. If we haven't got the record length yet, get
	 * the next four bytes. Otherwise try to gobble up as much as
	 * possible up to the complete record length.
	 */
	if (svsk->sk_tcplen < 4) {
		unsigned long	want = 4 - svsk->sk_tcplen;
		struct kvec	iov;

		iov.iov_base = ((char *) &svsk->sk_reclen) + svsk->sk_tcplen;
		iov.iov_len  = want;
		if ((len = svc_recvfrom(rqstp, &iov, 1, want)) < 0)
			goto error;
		svsk->sk_tcplen += len;

		if (len < want) {
			dprintk("svc: short recvfrom while reading record length (%d of %lu)\n",
				len, want);
			svc_xprt_received(&svsk->sk_xprt);
			return -EAGAIN; /* record header not complete */
		}

		svsk->sk_reclen = ntohl(svsk->sk_reclen);
		if (!(svsk->sk_reclen & 0x80000000)) {
			/* FIXME: technically, a record can be fragmented,
			 *  and non-terminal fragments will not have the top
			 *  bit set in the fragment length header.
			 *  But apparently no known nfs clients send fragmented
			 *  records. */
			if (net_ratelimit())
				printk(KERN_NOTICE "RPC: bad TCP reclen 0x%08lx"
				       " (non-terminal)\n",
				       (unsigned long) svsk->sk_reclen);
			goto err_delete;
		}
		svsk->sk_reclen &= 0x7fffffff;
		dprintk("svc: TCP record, %d bytes\n", svsk->sk_reclen);
		if (svsk->sk_reclen > serv->sv_max_mesg) {
			if (net_ratelimit())
				printk(KERN_NOTICE "RPC: bad TCP reclen 0x%08lx"
				       " (large)\n",
				       (unsigned long) svsk->sk_reclen);
			goto err_delete;
		}
	}

	/* Check whether enough data is available */
	len = svc_recv_available(svsk);
	if (len < 0)
		goto error;

	if (len < svsk->sk_reclen) {
		dprintk("svc: incomplete TCP record (%d of %d)\n",
			len, svsk->sk_reclen);
		svc_xprt_received(&svsk->sk_xprt);
		return -EAGAIN;	/* record not complete */
	}
	len = svsk->sk_reclen;
	set_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags);

	vec = rqstp->rq_vec;
	vec[0] = rqstp->rq_arg.head[0];
	vlen = PAGE_SIZE;
	pnum = 1;
	while (vlen < len) {
		vec[pnum].iov_base = page_address(rqstp->rq_pages[pnum]);
		vec[pnum].iov_len = PAGE_SIZE;
		pnum++;
		vlen += PAGE_SIZE;
	}
	rqstp->rq_respages = &rqstp->rq_pages[pnum];

	/* Now receive data */
	len = svc_recvfrom(rqstp, vec, pnum, len);
	if (len < 0)
		goto error;

	dprintk("svc: TCP complete record (%d bytes)\n", len);
	rqstp->rq_arg.len = len;
	rqstp->rq_arg.page_base = 0;
	if (len <= rqstp->rq_arg.head[0].iov_len) {
		rqstp->rq_arg.head[0].iov_len = len;
		rqstp->rq_arg.page_len = 0;
	} else {
		rqstp->rq_arg.page_len = len - rqstp->rq_arg.head[0].iov_len;
	}

	rqstp->rq_xprt_ctxt   = NULL;
	rqstp->rq_prot	      = IPPROTO_TCP;

	/* Reset TCP read info */
	svsk->sk_reclen = 0;
	svsk->sk_tcplen = 0;

	svc_xprt_copy_addrs(rqstp, &svsk->sk_xprt);
	svc_xprt_received(&svsk->sk_xprt);
	if (serv->sv_stats)
		serv->sv_stats->nettcpcnt++;

	return len;

 err_delete:
	set_bit(XPT_CLOSE, &svsk->sk_xprt.xpt_flags);
	return -EAGAIN;

 error:
	if (len == -EAGAIN) {
		dprintk("RPC: TCP recvfrom got EAGAIN\n");
		svc_xprt_received(&svsk->sk_xprt);
	} else {
		printk(KERN_NOTICE "%s: recvfrom returned errno %d\n",
		       svsk->sk_xprt.xpt_server->sv_name, -len);
		goto err_delete;
	}

	return len;
}

/*
 * Send out data on TCP socket.
 */
static int
svc_tcp_sendto(struct svc_rqst *rqstp)
{
	struct xdr_buf	*xbufp = &rqstp->rq_res;
	int sent;
	__be32 reclen;

	/* Set up the first element of the reply kvec.
	 * Any other kvecs that may be in use have been taken
	 * care of by the server implementation itself.
	 */
	reclen = htonl(0x80000000|((xbufp->len ) - 4));
	memcpy(xbufp->head[0].iov_base, &reclen, 4);

	if (test_bit(XPT_DEAD, &rqstp->rq_sock->sk_xprt.xpt_flags))
		return -ENOTCONN;

	sent = svc_sendto(rqstp, &rqstp->rq_res);
	if (sent != xbufp->len) {
		printk(KERN_NOTICE "rpc-srv/tcp: %s: %s %d when sending %d bytes - shutting down socket\n",
		       rqstp->rq_sock->sk_xprt.xpt_server->sv_name,
		       (sent<0)?"got error":"sent only",
		       sent, xbufp->len);
		set_bit(XPT_CLOSE, &rqstp->rq_sock->sk_xprt.xpt_flags);
		svc_xprt_enqueue(rqstp->rq_xprt);
		sent = -EAGAIN;
	}
	return sent;
}

/*
 * Setup response header. TCP has a 4B record length field.
 */
static void svc_tcp_prep_reply_hdr(struct svc_rqst *rqstp)
{
	struct kvec *resv = &rqstp->rq_res.head[0];

	/* tcp needs a space for the record length... */
	svc_putnl(resv, 0);
}

static int svc_tcp_has_wspace(struct svc_xprt *xprt)
{
	struct svc_sock *svsk = container_of(xprt, struct svc_sock, sk_xprt);
	struct svc_serv	*serv = svsk->sk_xprt.xpt_server;
	int required;
	int wspace;

	/*
	 * Set the SOCK_NOSPACE flag before checking the available
	 * sock space.
	 */
	set_bit(SOCK_NOSPACE, &svsk->sk_sock->flags);
	required = atomic_read(&svsk->sk_xprt.xpt_reserved) + serv->sv_max_mesg;
	wspace = sk_stream_wspace(svsk->sk_sk);

	if (wspace < sk_stream_min_wspace(svsk->sk_sk))
		return 0;
	if (required * 2 > wspace)
		return 0;

	clear_bit(SOCK_NOSPACE, &svsk->sk_sock->flags);
	return 1;
}

static struct svc_xprt *svc_tcp_create(struct svc_serv *serv,
				       struct sockaddr *sa, int salen,
				       int flags)
{
	return svc_create_socket(serv, IPPROTO_TCP, sa, salen, flags);
}

static struct svc_xprt_ops svc_tcp_ops = {
	.xpo_create = svc_tcp_create,
	.xpo_recvfrom = svc_tcp_recvfrom,
	.xpo_sendto = svc_tcp_sendto,
	.xpo_release_rqst = svc_release_skb,
	.xpo_detach = svc_sock_detach,
	.xpo_free = svc_sock_free,
	.xpo_prep_reply_hdr = svc_tcp_prep_reply_hdr,
	.xpo_has_wspace = svc_tcp_has_wspace,
	.xpo_accept = svc_tcp_accept,
};

static struct svc_xprt_class svc_tcp_class = {
	.xcl_name = "tcp",
	.xcl_owner = THIS_MODULE,
	.xcl_ops = &svc_tcp_ops,
	.xcl_max_payload = RPCSVC_MAXPAYLOAD_TCP,
};

void svc_init_xprt_sock(void)
{
	svc_reg_xprt_class(&svc_tcp_class);
	svc_reg_xprt_class(&svc_udp_class);
}

void svc_cleanup_xprt_sock(void)
{
	svc_unreg_xprt_class(&svc_tcp_class);
	svc_unreg_xprt_class(&svc_udp_class);
}

static void svc_tcp_init(struct svc_sock *svsk, struct svc_serv *serv)
{
	struct sock	*sk = svsk->sk_sk;
	struct tcp_sock *tp = tcp_sk(sk);

	svc_xprt_init(&svc_tcp_class, &svsk->sk_xprt, serv);
	set_bit(XPT_CACHE_AUTH, &svsk->sk_xprt.xpt_flags);
	if (sk->sk_state == TCP_LISTEN) {
		dprintk("setting up TCP socket for listening\n");
		set_bit(XPT_LISTENER, &svsk->sk_xprt.xpt_flags);
		sk->sk_data_ready = svc_tcp_listen_data_ready;
		set_bit(XPT_CONN, &svsk->sk_xprt.xpt_flags);
	} else {
		dprintk("setting up TCP socket for reading\n");
		sk->sk_state_change = svc_tcp_state_change;
		sk->sk_data_ready = svc_tcp_data_ready;
		sk->sk_write_space = svc_write_space;

		svsk->sk_reclen = 0;
		svsk->sk_tcplen = 0;

		tp->nonagle = 1;        /* disable Nagle's algorithm */

		/* initialise setting must have enough space to
		 * receive and respond to one request.
		 * svc_tcp_recvfrom will re-adjust if necessary
		 */
		svc_sock_setbufsize(svsk->sk_sock,
				    3 * svsk->sk_xprt.xpt_server->sv_max_mesg,
				    3 * svsk->sk_xprt.xpt_server->sv_max_mesg);

		set_bit(XPT_CHNGBUF, &svsk->sk_xprt.xpt_flags);
		set_bit(XPT_DATA, &svsk->sk_xprt.xpt_flags);
		if (sk->sk_state != TCP_ESTABLISHED)
			set_bit(XPT_CLOSE, &svsk->sk_xprt.xpt_flags);
	}
}

void
svc_sock_update_bufs(struct svc_serv *serv)
{
	/*
	 * The number of server threads has changed. Update
	 * rcvbuf and sndbuf accordingly on all sockets
	 */
	struct list_head *le;

	spin_lock_bh(&serv->sv_lock);
	list_for_each(le, &serv->sv_permsocks) {
		struct svc_sock *svsk =
			list_entry(le, struct svc_sock, sk_xprt.xpt_list);
		set_bit(XPT_CHNGBUF, &svsk->sk_xprt.xpt_flags);
	}
	list_for_each(le, &serv->sv_tempsocks) {
		struct svc_sock *svsk =
			list_entry(le, struct svc_sock, sk_xprt.xpt_list);
		set_bit(XPT_CHNGBUF, &svsk->sk_xprt.xpt_flags);
	}
	spin_unlock_bh(&serv->sv_lock);
}

/*
 * Make sure that we don't have too many active connections.  If we
 * have, something must be dropped.
 *
 * There's no point in trying to do random drop here for DoS
 * prevention. The NFS clients does 1 reconnect in 15 seconds. An
 * attacker can easily beat that.
 *
 * The only somewhat efficient mechanism would be if drop old
 * connections from the same IP first. But right now we don't even
 * record the client IP in svc_sock.
 */
static void svc_check_conn_limits(struct svc_serv *serv)
{
	if (serv->sv_tmpcnt > (serv->sv_nrthreads+3)*20) {
		struct svc_sock *svsk = NULL;
		spin_lock_bh(&serv->sv_lock);
		if (!list_empty(&serv->sv_tempsocks)) {
			if (net_ratelimit()) {
				/* Try to help the admin */
				printk(KERN_NOTICE "%s: too many open TCP "
				       "sockets, consider increasing the "
				       "number of nfsd threads\n",
				       serv->sv_name);
			}
			/*
			 * Always select the oldest socket. It's not fair,
			 * but so is life
			 */
			svsk = list_entry(serv->sv_tempsocks.prev,
					  struct svc_sock,
					  sk_xprt.xpt_list);
			set_bit(XPT_CLOSE, &svsk->sk_xprt.xpt_flags);
			svc_xprt_get(&svsk->sk_xprt);
		}
		spin_unlock_bh(&serv->sv_lock);

		if (svsk) {
			svc_xprt_enqueue(&svsk->sk_xprt);
			svc_xprt_put(&svsk->sk_xprt);
		}
	}
}

/*
 * Receive the next request on any socket.  This code is carefully
 * organised not to touch any cachelines in the shared svc_serv
 * structure, only cachelines in the local svc_pool.
 */
int
svc_recv(struct svc_rqst *rqstp, long timeout)
{
	struct svc_xprt		*xprt = NULL;
	struct svc_serv		*serv = rqstp->rq_server;
	struct svc_pool		*pool = rqstp->rq_pool;
	int			len, i;
	int			pages;
	struct xdr_buf		*arg;
	DECLARE_WAITQUEUE(wait, current);

	dprintk("svc: server %p waiting for data (to = %ld)\n",
		rqstp, timeout);

	if (rqstp->rq_xprt)
		printk(KERN_ERR
			"svc_recv: service %p, transport not NULL!\n",
			 rqstp);
	if (waitqueue_active(&rqstp->rq_wait))
		printk(KERN_ERR
			"svc_recv: service %p, wait queue active!\n",
			 rqstp);


	/* now allocate needed pages.  If we get a failure, sleep briefly */
	pages = (serv->sv_max_mesg + PAGE_SIZE) / PAGE_SIZE;
	for (i=0; i < pages ; i++)
		while (rqstp->rq_pages[i] == NULL) {
			struct page *p = alloc_page(GFP_KERNEL);
			if (!p)
				schedule_timeout_uninterruptible(msecs_to_jiffies(500));
			rqstp->rq_pages[i] = p;
		}
	rqstp->rq_pages[i++] = NULL; /* this might be seen in nfs_read_actor */
	BUG_ON(pages >= RPCSVC_MAXPAGES);

	/* Make arg->head point to first page and arg->pages point to rest */
	arg = &rqstp->rq_arg;
	arg->head[0].iov_base = page_address(rqstp->rq_pages[0]);
	arg->head[0].iov_len = PAGE_SIZE;
	arg->pages = rqstp->rq_pages + 1;
	arg->page_base = 0;
	/* save at least one page for response */
	arg->page_len = (pages-2)*PAGE_SIZE;
	arg->len = (pages-1)*PAGE_SIZE;
	arg->tail[0].iov_len = 0;

	try_to_freeze();
	cond_resched();
	if (signalled())
		return -EINTR;

	spin_lock_bh(&pool->sp_lock);
	xprt = svc_xprt_dequeue(pool);
	if (xprt) {
		rqstp->rq_xprt = xprt;
		svc_xprt_get(xprt);
		rqstp->rq_reserved = serv->sv_max_mesg;
		atomic_add(rqstp->rq_reserved, &xprt->xpt_reserved);
	} else {
		/* No data pending. Go to sleep */
		svc_thread_enqueue(pool, rqstp);

		/*
		 * We have to be able to interrupt this wait
		 * to bring down the daemons ...
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&rqstp->rq_wait, &wait);
		spin_unlock_bh(&pool->sp_lock);

		schedule_timeout(timeout);

		try_to_freeze();

		spin_lock_bh(&pool->sp_lock);
		remove_wait_queue(&rqstp->rq_wait, &wait);

		xprt = rqstp->rq_xprt;
		if (!xprt) {
			svc_thread_dequeue(pool, rqstp);
			spin_unlock_bh(&pool->sp_lock);
			dprintk("svc: server %p, no data yet\n", rqstp);
			return signalled()? -EINTR : -EAGAIN;
		}
	}
	spin_unlock_bh(&pool->sp_lock);

	len = 0;
	if (test_bit(XPT_CLOSE, &xprt->xpt_flags)) {
		dprintk("svc_recv: found XPT_CLOSE\n");
		svc_delete_xprt(xprt);
	} else if (test_bit(XPT_LISTENER, &xprt->xpt_flags)) {
		struct svc_xprt *newxpt;
		newxpt = xprt->xpt_ops->xpo_accept(xprt);
		if (newxpt) {
			/*
			 * We know this module_get will succeed because the
			 * listener holds a reference too
			 */
			__module_get(newxpt->xpt_class->xcl_owner);
			svc_check_conn_limits(xprt->xpt_server);
			spin_lock_bh(&serv->sv_lock);
			set_bit(XPT_TEMP, &newxpt->xpt_flags);
			list_add(&newxpt->xpt_list, &serv->sv_tempsocks);
			serv->sv_tmpcnt++;
			if (serv->sv_temptimer.function == NULL) {
				/* setup timer to age temp sockets */
				setup_timer(&serv->sv_temptimer,
					    svc_age_temp_xprts,
					    (unsigned long)serv);
				mod_timer(&serv->sv_temptimer,
					  jiffies + svc_conn_age_period * HZ);
			}
			spin_unlock_bh(&serv->sv_lock);
			svc_xprt_received(newxpt);
		}
		svc_xprt_received(xprt);
	} else {
		dprintk("svc: server %p, pool %u, transport %p, inuse=%d\n",
			rqstp, pool->sp_id, xprt,
			atomic_read(&xprt->xpt_ref.refcount));
		rqstp->rq_deferred = svc_deferred_dequeue(xprt);
		if (rqstp->rq_deferred) {
			svc_xprt_received(xprt);
			len = svc_deferred_recv(rqstp);
		} else
			len = xprt->xpt_ops->xpo_recvfrom(rqstp);
		dprintk("svc: got len=%d\n", len);
	}

	/* No data, incomplete (TCP) read, or accept() */
	if (len == 0 || len == -EAGAIN) {
		rqstp->rq_res.len = 0;
		svc_xprt_release(rqstp);
		return -EAGAIN;
	}
	clear_bit(XPT_OLD, &xprt->xpt_flags);

	rqstp->rq_secure = svc_port_is_privileged(svc_addr(rqstp));
	rqstp->rq_chandle.defer = svc_defer;

	if (serv->sv_stats)
		serv->sv_stats->netcnt++;
	return len;
}

/*
 * Drop request
 */
void
svc_drop(struct svc_rqst *rqstp)
{
	dprintk("svc: socket %p dropped request\n", rqstp->rq_sock);
	svc_xprt_release(rqstp);
}

/*
 * Return reply to client.
 */
int
svc_send(struct svc_rqst *rqstp)
{
	struct svc_xprt	*xprt;
	int		len;
	struct xdr_buf	*xb;

	xprt = rqstp->rq_xprt;
	if (!xprt)
		return -EFAULT;

	/* release the receive skb before sending the reply */
	rqstp->rq_xprt->xpt_ops->xpo_release_rqst(rqstp);

	/* calculate over-all length */
	xb = & rqstp->rq_res;
	xb->len = xb->head[0].iov_len +
		xb->page_len +
		xb->tail[0].iov_len;

	/* Grab mutex to serialize outgoing data. */
	mutex_lock(&xprt->xpt_mutex);
	if (test_bit(XPT_DEAD, &xprt->xpt_flags))
		len = -ENOTCONN;
	else
		len = xprt->xpt_ops->xpo_sendto(rqstp);
	mutex_unlock(&xprt->xpt_mutex);
	svc_xprt_release(rqstp);

	if (len == -ECONNREFUSED || len == -ENOTCONN || len == -EAGAIN)
		return 0;
	return len;
}

/*
 * Timer function to close old temporary sockets, using
 * a mark-and-sweep algorithm.
 */
static void svc_age_temp_xprts(unsigned long closure)
{
	struct svc_serv *serv = (struct svc_serv *)closure;
	struct svc_xprt *xprt;
	struct list_head *le, *next;
	LIST_HEAD(to_be_aged);

	dprintk("svc_age_temp_xprts\n");

	if (!spin_trylock_bh(&serv->sv_lock)) {
		/* busy, try again 1 sec later */
		dprintk("svc_age_temp_xprts: busy\n");
		mod_timer(&serv->sv_temptimer, jiffies + HZ);
		return;
	}

	list_for_each_safe(le, next, &serv->sv_tempsocks) {
		xprt = list_entry(le, struct svc_xprt, xpt_list);

		/* First time through, just mark it OLD. Second time
		 * through, close it. */
		if (!test_and_set_bit(XPT_OLD, &xprt->xpt_flags))
			continue;
		if (atomic_read(&xprt->xpt_ref.refcount) > 1
		    || test_bit(XPT_BUSY, &xprt->xpt_flags))
			continue;
		svc_xprt_get(xprt);
		list_move(le, &to_be_aged);
		set_bit(XPT_CLOSE, &xprt->xpt_flags);
		set_bit(XPT_DETACHED, &xprt->xpt_flags);
	}
	spin_unlock_bh(&serv->sv_lock);

	while (!list_empty(&to_be_aged)) {
		le = to_be_aged.next;
		/* fiddling the xpt_list node is safe 'cos we're XPT_DETACHED */
		list_del_init(le);
		xprt = list_entry(le, struct svc_xprt, xpt_list);

		dprintk("queuing xprt %p for closing\n", xprt);

		/* a thread will dequeue and close it soon */
		svc_xprt_enqueue(xprt);
		svc_xprt_put(xprt);
	}

	mod_timer(&serv->sv_temptimer, jiffies + svc_conn_age_period * HZ);
}

/*
 * Initialize socket for RPC use and create svc_sock struct
 * XXX: May want to setsockopt SO_SNDBUF and SO_RCVBUF.
 */
static struct svc_sock *svc_setup_socket(struct svc_serv *serv,
						struct socket *sock,
						int *errp, int flags)
{
	struct svc_sock	*svsk;
	struct sock	*inet;
	int		pmap_register = !(flags & SVC_SOCK_ANONYMOUS);

	dprintk("svc: svc_setup_socket %p\n", sock);
	if (!(svsk = kzalloc(sizeof(*svsk), GFP_KERNEL))) {
		*errp = -ENOMEM;
		return NULL;
	}

	inet = sock->sk;

	/* Register socket with portmapper */
	if (*errp >= 0 && pmap_register)
		*errp = svc_register(serv, inet->sk_protocol,
				     ntohs(inet_sk(inet)->sport));

	if (*errp < 0) {
		kfree(svsk);
		return NULL;
	}

	inet->sk_user_data = svsk;
	svsk->sk_sock = sock;
	svsk->sk_sk = inet;
	svsk->sk_ostate = inet->sk_state_change;
	svsk->sk_odata = inet->sk_data_ready;
	svsk->sk_owspace = inet->sk_write_space;

	/* Initialize the socket */
	if (sock->type == SOCK_DGRAM)
		svc_udp_init(svsk, serv);
	else
		svc_tcp_init(svsk, serv);

	dprintk("svc: svc_setup_socket created %p (inet %p)\n",
				svsk, svsk->sk_sk);

	return svsk;
}

int svc_addsock(struct svc_serv *serv,
		int fd,
		char *name_return,
		int *proto)
{
	int err = 0;
	struct socket *so = sockfd_lookup(fd, &err);
	struct svc_sock *svsk = NULL;

	if (!so)
		return err;
	if (so->sk->sk_family != AF_INET)
		err =  -EAFNOSUPPORT;
	else if (so->sk->sk_protocol != IPPROTO_TCP &&
	    so->sk->sk_protocol != IPPROTO_UDP)
		err =  -EPROTONOSUPPORT;
	else if (so->state > SS_UNCONNECTED)
		err = -EISCONN;
	else {
		svsk = svc_setup_socket(serv, so, &err, SVC_SOCK_DEFAULTS);
		if (svsk) {
			struct sockaddr_storage addr;
			struct sockaddr *sin = (struct sockaddr *)&addr;
			int salen;
			if (kernel_getsockname(svsk->sk_sock, sin, &salen) == 0)
				svc_xprt_set_local(&svsk->sk_xprt, sin, salen);
			clear_bit(XPT_TEMP, &svsk->sk_xprt.xpt_flags);
			spin_lock_bh(&serv->sv_lock);
			list_add(&svsk->sk_xprt.xpt_list, &serv->sv_permsocks);
			spin_unlock_bh(&serv->sv_lock);
			svc_xprt_received(&svsk->sk_xprt);
			err = 0;
		}
	}
	if (err) {
		sockfd_put(so);
		return err;
	}
	if (proto) *proto = so->sk->sk_protocol;
	return one_sock_name(name_return, svsk);
}
EXPORT_SYMBOL_GPL(svc_addsock);

/*
 * Create socket for RPC service.
 */
static struct svc_xprt *svc_create_socket(struct svc_serv *serv,
					  int protocol,
					  struct sockaddr *sin, int len,
					  int flags)
{
	struct svc_sock	*svsk;
	struct socket	*sock;
	int		error;
	int		type;
	char		buf[RPC_MAX_ADDRBUFLEN];
	struct sockaddr_storage addr;
	struct sockaddr *newsin = (struct sockaddr *)&addr;
	int		newlen;

	dprintk("svc: svc_create_socket(%s, %d, %s)\n",
			serv->sv_program->pg_name, protocol,
			__svc_print_addr(sin, buf, sizeof(buf)));

	if (protocol != IPPROTO_UDP && protocol != IPPROTO_TCP) {
		printk(KERN_WARNING "svc: only UDP and TCP "
				"sockets supported\n");
		return ERR_PTR(-EINVAL);
	}
	type = (protocol == IPPROTO_UDP)? SOCK_DGRAM : SOCK_STREAM;

	error = sock_create_kern(sin->sa_family, type, protocol, &sock);
	if (error < 0)
		return ERR_PTR(error);

	svc_reclassify_socket(sock);

	if (type == SOCK_STREAM)
		sock->sk->sk_reuse = 1;		/* allow address reuse */
	error = kernel_bind(sock, sin, len);
	if (error < 0)
		goto bummer;

	newlen = len;
	error = kernel_getsockname(sock, newsin, &newlen);
	if (error < 0)
		goto bummer;

	if (protocol == IPPROTO_TCP) {
		if ((error = kernel_listen(sock, 64)) < 0)
			goto bummer;
	}

	if ((svsk = svc_setup_socket(serv, sock, &error, flags)) != NULL) {
		svc_xprt_set_local(&svsk->sk_xprt, newsin, newlen);
		return (struct svc_xprt *)svsk;
	}

bummer:
	dprintk("svc: svc_create_socket error = %d\n", -error);
	sock_release(sock);
	return ERR_PTR(error);
}

/*
 * Detach the svc_sock from the socket so that no
 * more callbacks occur.
 */
static void svc_sock_detach(struct svc_xprt *xprt)
{
	struct svc_sock *svsk = container_of(xprt, struct svc_sock, sk_xprt);
	struct sock *sk = svsk->sk_sk;

	dprintk("svc: svc_sock_detach(%p)\n", svsk);

	/* put back the old socket callbacks */
	sk->sk_state_change = svsk->sk_ostate;
	sk->sk_data_ready = svsk->sk_odata;
	sk->sk_write_space = svsk->sk_owspace;
}

/*
 * Free the svc_sock's socket resources and the svc_sock itself.
 */
static void svc_sock_free(struct svc_xprt *xprt)
{
	struct svc_sock *svsk = container_of(xprt, struct svc_sock, sk_xprt);
	dprintk("svc: svc_sock_free(%p)\n", svsk);

	if (svsk->sk_sock->file)
		sockfd_put(svsk->sk_sock);
	else
		sock_release(svsk->sk_sock);
	kfree(svsk);
}

/*
 * Remove a dead transport
 */
static void svc_delete_xprt(struct svc_xprt *xprt)
{
	struct svc_serv	*serv = xprt->xpt_server;

	dprintk("svc: svc_delete_xprt(%p)\n", xprt);
	xprt->xpt_ops->xpo_detach(xprt);

	spin_lock_bh(&serv->sv_lock);
	if (!test_and_set_bit(XPT_DETACHED, &xprt->xpt_flags))
		list_del_init(&xprt->xpt_list);
	/*
	 * We used to delete the transport from whichever list
	 * it's sk_xprt.xpt_ready node was on, but we don't actually
	 * need to.  This is because the only time we're called
	 * while still attached to a queue, the queue itself
	 * is about to be destroyed (in svc_destroy).
	 */
	if (!test_and_set_bit(XPT_DEAD, &xprt->xpt_flags)) {
		BUG_ON(atomic_read(&xprt->xpt_ref.refcount) < 2);
		if (test_bit(XPT_TEMP, &xprt->xpt_flags))
			serv->sv_tmpcnt--;
		svc_xprt_put(xprt);
	}
	spin_unlock_bh(&serv->sv_lock);
}

static void svc_close_xprt(struct svc_xprt *xprt)
{
	set_bit(XPT_CLOSE, &xprt->xpt_flags);
	if (test_and_set_bit(XPT_BUSY, &xprt->xpt_flags))
		/* someone else will have to effect the close */
		return;

	svc_xprt_get(xprt);
	svc_delete_xprt(xprt);
	clear_bit(XPT_BUSY, &xprt->xpt_flags);
	svc_xprt_put(xprt);
}

void svc_close_all(struct list_head *xprt_list)
{
	struct svc_xprt *xprt;
	struct svc_xprt *tmp;

	list_for_each_entry_safe(xprt, tmp, xprt_list, xpt_list) {
		set_bit(XPT_CLOSE, &xprt->xpt_flags);
		if (test_bit(XPT_BUSY, &xprt->xpt_flags)) {
			/* Waiting to be processed, but no threads left,
			 * So just remove it from the waiting list
			 */
			list_del_init(&xprt->xpt_ready);
			clear_bit(XPT_BUSY, &xprt->xpt_flags);
		}
		svc_close_xprt(xprt);
	}
}

/*
 * Handle defer and revisit of requests
 */

static void svc_revisit(struct cache_deferred_req *dreq, int too_many)
{
	struct svc_deferred_req *dr = container_of(dreq, struct svc_deferred_req, handle);
	struct svc_xprt *xprt = dr->xprt;

	if (too_many) {
		svc_xprt_put(xprt);
		kfree(dr);
		return;
	}
	dprintk("revisit queued\n");
	dr->xprt = NULL;
	spin_lock(&xprt->xpt_lock);
	list_add(&dr->handle.recent, &xprt->xpt_deferred);
	spin_unlock(&xprt->xpt_lock);
	set_bit(XPT_DEFERRED, &xprt->xpt_flags);
	svc_xprt_enqueue(xprt);
	svc_xprt_put(xprt);
}

static struct cache_deferred_req *
svc_defer(struct cache_req *req)
{
	struct svc_rqst *rqstp = container_of(req, struct svc_rqst, rq_chandle);
	int size = sizeof(struct svc_deferred_req) + (rqstp->rq_arg.len);
	struct svc_deferred_req *dr;

	if (rqstp->rq_arg.page_len)
		return NULL; /* if more than a page, give up FIXME */
	if (rqstp->rq_deferred) {
		dr = rqstp->rq_deferred;
		rqstp->rq_deferred = NULL;
	} else {
		int skip  = rqstp->rq_arg.len - rqstp->rq_arg.head[0].iov_len;
		/* FIXME maybe discard if size too large */
		dr = kmalloc(size, GFP_KERNEL);
		if (dr == NULL)
			return NULL;

		dr->handle.owner = rqstp->rq_server;
		dr->prot = rqstp->rq_prot;
		memcpy(&dr->addr, &rqstp->rq_addr, rqstp->rq_addrlen);
		dr->addrlen = rqstp->rq_addrlen;
		dr->daddr = rqstp->rq_daddr;
		dr->argslen = rqstp->rq_arg.len >> 2;
		memcpy(dr->args, rqstp->rq_arg.head[0].iov_base-skip, dr->argslen<<2);
	}
	svc_xprt_get(rqstp->rq_xprt);
	dr->xprt = rqstp->rq_xprt;

	dr->handle.revisit = svc_revisit;
	return &dr->handle;
}

/*
 * recv data from a deferred request into an active one
 */
static int svc_deferred_recv(struct svc_rqst *rqstp)
{
	struct svc_deferred_req *dr = rqstp->rq_deferred;

	rqstp->rq_arg.head[0].iov_base = dr->args;
	rqstp->rq_arg.head[0].iov_len = dr->argslen<<2;
	rqstp->rq_arg.page_len = 0;
	rqstp->rq_arg.len = dr->argslen<<2;
	rqstp->rq_prot        = dr->prot;
	memcpy(&rqstp->rq_addr, &dr->addr, dr->addrlen);
	rqstp->rq_addrlen     = dr->addrlen;
	rqstp->rq_daddr       = dr->daddr;
	rqstp->rq_respages    = rqstp->rq_pages;
	return dr->argslen<<2;
}


static struct svc_deferred_req *svc_deferred_dequeue(struct svc_xprt *xprt)
{
	struct svc_deferred_req *dr = NULL;

	if (!test_bit(XPT_DEFERRED, &xprt->xpt_flags))
		return NULL;
	spin_lock(&xprt->xpt_lock);
	clear_bit(XPT_DEFERRED, &xprt->xpt_flags);
	if (!list_empty(&xprt->xpt_deferred)) {
		dr = list_entry(xprt->xpt_deferred.next,
				struct svc_deferred_req,
				handle.recent);
		list_del_init(&dr->handle.recent);
		set_bit(XPT_DEFERRED, &xprt->xpt_flags);
	}
	spin_unlock(&xprt->xpt_lock);
	return dr;
}
