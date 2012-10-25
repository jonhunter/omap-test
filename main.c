/*
 *  omap-test.c - OMAP Test module
 */
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/debugfs.h>

extern int omap_test_timer_init(struct dentry *d);
extern void omap_test_timer_exit(void);

static struct dentry *d;

int init_module(void)
{
	int r;

	d = debugfs_create_dir("omap-test", NULL);
	if (IS_ERR_OR_NULL(d)) {
		pr_err("OMAP Test: Failed to create omap-test directory!\n");
		return PTR_ERR(d);
	}

	r = omap_test_timer_init(d);
	if (r) {
		pr_err("OMAP Test: Failed to initialise timer tests!\n");
		return r;
	}

	pr_info("OMAP Test Module Loaded!\n");

	return 0;
}

void cleanup_module(void)
{
	omap_test_timer_exit();
	debugfs_remove_recursive(d);
	pr_info("OMAP Test Module Unloaded!\n");
}

MODULE_LICENSE("GPL v2");
