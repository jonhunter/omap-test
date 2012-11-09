/*
 *  timer.c - OMAP timer test code
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/clk.h>
#include <linux/ktime.h>
#include <mach/../../soc.h>
#include <plat/dmtimer.h>

#define OMAP1_NUM_TIMERS	8
#define OMAP2_NUM_TIMERS	12
#define OMAP4_NUM_TIMERS	11
#define AM335X_NUM_TIMERS	7
#define OMAP_MAX_NUM_TIMERS	12
#define OMAP_TIMER_SRC_CLKS	2
#define TIMER_TIMEOUT 		(msecs_to_jiffies(1000))

struct timer_irq_data {
	struct omap_dm_timer *gptimer;
	struct completion complete;
};

static u32 omap_test_all;
static u32 omap_test_one;
static u32 omap_test_stress;
static int omap_timer_stress;
static int omap_timer_test_stop;
static int omap_timer_test_running;
static int timer_source_clks[OMAP_TIMER_SRC_CLKS] = {OMAP_TIMER_SRC_SYS_CLK, OMAP_TIMER_SRC_32_KHZ};
struct timer_irq_data irq_data;
struct work_struct omap_timer_work;

static int omap_timer_read_error(struct omap_dm_timer *gptimer, u32 now,
				 u32 last, u32 loop)
{
	u32 error, reads = 0;

	error = now;

	do {
		now = omap_dm_timer_read_counter(gptimer);

	} while ((last >= now) && (reads++ < 1000));

	omap_dm_timer_stop(gptimer);

	pr_info("Timer read test FAILED on loop iteration %d!\n", loop);
	pr_info("Timer count at error %d and count before %d\n", error, last);

	if (reads < 1000)
		pr_info("Read timer %d times to recover, timer count now %d\n",
			reads, now);
	else
		pr_info("Unable to recover timer!\n");

	return -EIO;

}

static int omap_timer_read_test(struct omap_dm_timer *gptimer, int delay,
					int loop)
{
	u32 i, now, last = 0;

	omap_dm_timer_set_load_start(gptimer, 0, 12345678);

	for (i = 0; i < loop; i++) {
		msleep(delay);
		now = omap_dm_timer_read_counter(gptimer);

		if (last >= now)
			return omap_timer_read_error(gptimer, now, last, i);

		last = now;
	}

	omap_dm_timer_stop(gptimer);

	pr_info("Timer read test PASSED! No errors, %d loops\n", loop);

	return 0;
}

static irqreturn_t omap_timer_interrupt(int irq, void *dev_id)
{
	struct timer_irq_data *d = dev_id;
	u32 l;

	l = omap_dm_timer_read_status(d->gptimer);
	omap_dm_timer_write_status(d->gptimer, l);

	if (!l)
		pr_err("ERROR: Timer interrupt but no interrupts pending!\n");

	complete(&d->complete);

	return IRQ_HANDLED;
}

static int omap_timer_interrupt_test(struct omap_dm_timer *gptimer)
{
	int r, timer_irq, timeout;

	timer_irq = omap_dm_timer_get_irq(gptimer);

	if (request_irq(timer_irq, omap_timer_interrupt,
			IRQF_TRIGGER_HIGH, "timer-test", &irq_data) < 0) {
		pr_err("Failed to allocate timer interrupt (%d)\n", timer_irq);
		return -ENODEV;
	}

	irq_data.gptimer = gptimer;
	init_completion(&irq_data.complete);
	omap_dm_timer_set_int_enable(gptimer, OMAP_TIMER_INT_OVERFLOW);
	omap_dm_timer_set_load_start(gptimer, 0, 0xffffff00);

	timeout = wait_for_completion_timeout(&irq_data.complete, TIMER_TIMEOUT);

	if (timeout == 0) {
		pr_err("Timer interrupt test FAILED! No interrupt occurred in 1 sec\n");
		r = -ETIMEDOUT;
	} else {
		pr_info("Timer interrupt test PASSED!\n");
		r = 0;
	}

	omap_dm_timer_stop(gptimer);
	omap_dm_timer_set_int_enable(gptimer, 0);
	free_irq(timer_irq, &irq_data);

	return r;
}

static u32 omap_timer_num_timers(void)
{
	u32 max_num_timers = 0;

#ifdef CONFIG_ARCH_OMAP1
	if (cpu_class_is_omap1())
		max_num_timers = OMAP1_NUM_TIMERS;
#else
	if (cpu_is_omap34xx() && (omap_type() != OMAP2_DEVICE_TYPE_GP))
		max_num_timers = OMAP2_NUM_TIMERS - 1;
	else if (cpu_is_omap24xx() || cpu_is_omap34xx())
		max_num_timers = OMAP2_NUM_TIMERS;
	else if (soc_is_am335x())
		max_num_timers = AM335X_NUM_TIMERS;
	else
		max_num_timers = OMAP4_NUM_TIMERS;
#endif
	return max_num_timers;
}

static struct omap_dm_timer *omap_timer_request_one(u32 timer_id)
{
	struct omap_dm_timer *gptimer;

	if (timer_id)
		gptimer = omap_dm_timer_request_specific(timer_id);
	else
		gptimer = omap_dm_timer_request();

	if (gptimer)
		return gptimer;

	return NULL;
}

static int omap_timer_run_tests(struct omap_dm_timer *gptimer)
{
	int i, r;

	for (i = 0, r = 0; i < OMAP_TIMER_SRC_CLKS; i++) {
		if (omap_dm_timer_set_source(gptimer, timer_source_clks[i])) {
			pr_err("ERROR: Failed to set timer %d source clock!\n",
				gptimer->id);
			r++;
			continue;
		}

#ifdef CONFIG_ARCH_OMAP1
		pr_info("Testing %s ...\n", dev_name(&gptimer->pdev->dev));
#else
		pr_info("Testing %s with %lu Hz clock ...\n",
			dev_name(&gptimer->pdev->dev),
			clk_get_rate(omap_dm_timer_get_fclk(gptimer)));
#endif
		if (omap_timer_read_test(gptimer, 10, 100))
			r++;
		if (omap_timer_interrupt_test(gptimer))
			r++;
	}

	return r;
}

static void omap_timer_test_one(u32 timer_id)
{
	struct omap_dm_timer *gptimer;

	gptimer = omap_timer_request_one(timer_id);

	if (gptimer) {
		omap_timer_run_tests(gptimer);
		omap_dm_timer_free(gptimer);
		return;
	}

	if (timer_id)
		pr_info("Timer %d not available!\n", timer_id);
	else
		pr_info("No timers available!\n");
}

static int omap_timer_test_all(void)
{
	struct omap_dm_timer *gptimers[OMAP_MAX_NUM_TIMERS];
	int i, r, num_timers, count, errors;

	count = errors = 0;

	num_timers = omap_timer_num_timers();

	for (i = 0; i < num_timers; i++) {
		gptimers[count] = omap_timer_request_one(0);
		if (gptimers[count])
			count++;
	}

	for (i = 0; i < count; i++) {
		r = omap_timer_run_tests(gptimers[i]);
		if (r)
			errors += r;
		if (omap_timer_test_stop)
			break;
	}

	for (i = 0; i < count; i++)
		omap_dm_timer_free(gptimers[i]);

	pr_info("Tested %d timers, skipped %d timers and detected %d errors\n",
		count, (num_timers - count), errors);

	return errors;
}

static void omap_timer_work_fn(struct work_struct *work)
{
	u32 err, count = 0;
	u64 ktime_start, ktime_end;

	do {
		ktime_start = ktime_to_ms(ktime_get());
		err = omap_timer_test_all();
		ktime_end = ktime_to_ms(ktime_get());

		pr_info("Test iteration %d complete in %d secs\n", count++,
			(u32)(ktime_end - ktime_start)/1000);

		if (ktime_end <= ktime_start)
			err++;

	} while (!err && omap_timer_stress);

	pr_info("Test summary: Iterations %d, Errors %d\n", count, err);

	omap_timer_test_running = 0;
}

static int option_get(void *data, u64 *v)
{
	u32 *option = data;

	*v = *option;

	return 0;
}

static int option_set(void *data, u64 v)
{
	u32 *option = data;

	*option = v;

	if (omap_timer_test_running) {
		if ((option != &omap_test_all) &&
		    (option != &omap_test_stress)) {
			return 0;
		} else if (*option) {
			return 0;
		} else if (*option == 0) {
			omap_timer_test_stop = 1;
			omap_timer_stress = 0;
			return 0;
		}
	} else if (*option == 0) {
		return 0;
	}

	if (option == &omap_test_one) {
		omap_timer_test_running = 1;
		omap_timer_test_one(*option);
		omap_timer_test_running = 0;
		return 0;
	}

	if (option == &omap_test_stress)
		omap_timer_stress = 1;

	if ((option == &omap_test_all) || (option == &omap_test_stress)) {
		omap_timer_test_stop = 0;
		omap_timer_test_running = 1;
		schedule_work(&omap_timer_work);
		return 0;
	}

	return -EINVAL;
}

DEFINE_SIMPLE_ATTRIBUTE(omap_test_option_fops, option_get, option_set, "%llu\n");

int omap_test_timer_init(struct dentry *d)
{
	d = debugfs_create_dir("timer", d);
	if (IS_ERR_OR_NULL(d))
		return PTR_ERR(d);
	(void) debugfs_create_file("all", S_IRUGO | S_IWUSR, d,
		&omap_test_all, &omap_test_option_fops);
	(void) debugfs_create_file("one", S_IRUGO | S_IWUSR, d,
		&omap_test_one, &omap_test_option_fops);
	(void) debugfs_create_file("stress", S_IRUGO | S_IWUSR, d,
		&omap_test_stress, &omap_test_option_fops);

	INIT_WORK(&omap_timer_work, omap_timer_work_fn);

	omap_timer_test_stop = 0;
	omap_timer_test_running = 0;

	return 0;
}

void omap_test_timer_exit(void)
{
	while (omap_timer_test_running) {
		omap_timer_test_stop = 1;
		omap_timer_stress = 0;
		msleep(10);
	}
}
