#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/timer.h>
#include <linux/export.h>
#include <net/genetlink.h>

#include "../genl_ex.h"

static struct timer_list timer;
static struct genl_family genl_test_families[GENL_TEST_FAMILY_COUNT];

/* Forward declarations */
static void genl_test_free_mcgrps(unsigned int family_idx);

static void greet_group(unsigned int family_idx, unsigned int local_group)
{	
	void *hdr;
	int res, flags = GFP_ATOMIC;
	char msg[GENL_TEST_ATTR_MSG_MAX];
	char group_name[32];
	struct sk_buff* skb = genlmsg_new(NLMSG_DEFAULT_SIZE, flags);

	if (!skb) {
		printk(KERN_ERR "%d: OOM!!", __LINE__);
		return;
	}

	if (family_idx >= GENL_TEST_FAMILY_COUNT) {
		printk(KERN_ERR "%d: Invalid family index %u\n", __LINE__, family_idx);
		nlmsg_free(skb);
		return;
	}

	hdr = genlmsg_put(skb, 0, 0, &genl_test_families[family_idx], flags, GENL_TEST_C_MSG);
	if (!hdr) {
		printk(KERN_ERR "%d: Unknown err !", __LINE__);
		goto nlmsg_fail;
	}

	snprintf(group_name, sizeof(group_name), "genl_mcgrp%u", local_group);
	snprintf(msg, GENL_TEST_ATTR_MSG_MAX, "Hello from family %u, group %s\n",
			family_idx, group_name);

	res = nla_put_string(skb, GENL_TEST_ATTR_MSG, msg);
	if (res) {
		printk(KERN_ERR "%d: err %d ", __LINE__, res);
		goto nlmsg_fail;
	}

	genlmsg_end(skb, hdr);
	res = genlmsg_multicast(&genl_test_families[family_idx], skb, 0, local_group, flags);
	if (res < 0) {
		printk(KERN_ERR "%d: Failed to multicast: %d\n", __LINE__, res);
		/* skb will be freed by genlmsg_multicast on error */
		return;
	}
	return;

nlmsg_fail:
	genlmsg_cancel(skb, hdr);
	nlmsg_free(skb);
	return;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,14,0)
static void genl_test_periodic(unsigned long data)
{
	(void)data; /* Suppress unused parameter warning */
#else
static void genl_test_periodic(struct timer_list *unused)
{
	(void)unused; /* Suppress unused parameter warning */
#endif
	/* Greet first group of each family as example */
	unsigned int i;
	for (i = 0; i < GENL_TEST_FAMILY_COUNT; i++) {
		greet_group(i, 0); /* Greet group 0 of each family */
	}

	mod_timer(&timer, jiffies + msecs_to_jiffies(GENL_TEST_HELLO_INTERVAL));
}

static int genl_test_rx_msg(struct sk_buff* skb, struct genl_info* info)
{
	if (!info->attrs[GENL_TEST_ATTR_MSG]) {
		printk(KERN_ERR "empty message from %d!!\n",
			info->snd_portid);
		printk(KERN_ERR "%p\n", info->attrs[GENL_TEST_ATTR_MSG]);
		return -EINVAL;
	}

	printk(KERN_NOTICE "Family %s, port %u says %s \n",
		info->family->name, info->snd_portid,
		(char*)nla_data(info->attrs[GENL_TEST_ATTR_MSG]));
	return 0;
}

static const struct genl_ops genl_test_ops[] = {
	{
		.cmd = GENL_TEST_C_MSG,
		.policy = genl_test_policy,
		.doit = genl_test_rx_msg,
		.dumpit = NULL,
	},
};

/* Store allocated group names separately */
static char *genl_test_mcgrp_names[GENL_TEST_FAMILY_COUNT][GENL_TEST_GROUPS_PER_FAMILY];

/* Helper function to generate group names dynamically */
static int genl_test_init_mcgrps(struct genl_multicast_group *mcgrps, unsigned int family_idx)
{
	unsigned int i;
	char name[32];
	
	for (i = 0; i < GENL_TEST_GROUPS_PER_FAMILY; i++) {
		snprintf(name, sizeof(name), "genl_mcgrp%u", i);
		genl_test_mcgrp_names[family_idx][i] = kstrdup(name, GFP_KERNEL);
		if (!genl_test_mcgrp_names[family_idx][i]) {
			printk(KERN_ERR "Failed to allocate group name for family %u, group %u\n",
				family_idx, i);
			/* Cleanup already allocated groups */
			genl_test_free_mcgrps(family_idx);
			return -ENOMEM;
		}
		/* Initialize the structure properly */
		/* Use a non-const pointer to assign to const field (kernel pattern) */
		{
			const char **name_ptr = (const char **)&mcgrps[i].name;
			*name_ptr = genl_test_mcgrp_names[family_idx][i];
		}
	}
	return 0;
}

static void genl_test_free_mcgrps(unsigned int family_idx)
{
	unsigned int i;
	for (i = 0; i < GENL_TEST_GROUPS_PER_FAMILY; i++) {
		if (genl_test_mcgrp_names[family_idx][i]) {
			kfree(genl_test_mcgrp_names[family_idx][i]);
			genl_test_mcgrp_names[family_idx][i] = NULL;
		}
	}
}

static struct genl_multicast_group genl_test_mcgrps[GENL_TEST_FAMILY_COUNT][GENL_TEST_GROUPS_PER_FAMILY];

static int __init genl_test_init(void)
{
	int rc;
	unsigned int i, registered_count = 0;
	char family_name[32];

	printk(KERN_INFO "genl_test: initializing %u netlink families\n", GENL_TEST_FAMILY_COUNT);

	/* Initialize multicast groups for each family */
	for (i = 0; i < GENL_TEST_FAMILY_COUNT; i++) {
		rc = genl_test_init_mcgrps(genl_test_mcgrps[i], i);
		if (rc) {
			/* Cleanup already initialized families */
			unsigned int j;
			for (j = 0; j < i; j++) {
				genl_test_free_mcgrps(j);
			}
			goto failure;
		}
	}

	/* Register all families */
	for (i = 0; i < GENL_TEST_FAMILY_COUNT; i++) {
		snprintf(family_name, sizeof(family_name), "%s%u", GENL_TEST_FAMILY_BASE_NAME, i);
		
		/* Use a non-const pointer to assign to const field (kernel pattern) */
		{
			char *allocated_name = kstrdup(family_name, GFP_KERNEL);
			if (!allocated_name) {
				printk(KERN_ERR "Failed to allocate family name for family %u\n", i);
				goto failure;
			}
			const char **name_ptr = (const char **)&genl_test_families[i].name;
			*name_ptr = allocated_name;
		}
		genl_test_families[i].version = 1;
		genl_test_families[i].maxattr = GENL_TEST_ATTR_MAX;
		genl_test_families[i].netnsok = false;
		genl_test_families[i].module = THIS_MODULE;
		genl_test_families[i].ops = genl_test_ops;
		genl_test_families[i].n_ops = ARRAY_SIZE(genl_test_ops);
		genl_test_families[i].mcgrps = genl_test_mcgrps[i];
		genl_test_families[i].n_mcgrps = GENL_TEST_GROUPS_PER_FAMILY;

		rc = genl_register_family(&genl_test_families[i]);
		if (rc) {
			printk(KERN_ERR "Failed to register family %u: %d\n", i, rc);
			kfree(genl_test_families[i].name);
			genl_test_families[i].name = NULL;
			goto failure;
		}
		registered_count++;
		printk(KERN_INFO "Registered family %s with %u groups\n",
			family_name, GENL_TEST_GROUPS_PER_FAMILY);
	}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,14,0)
	init_timer(&timer);
	timer.data = 0;
	timer.function = genl_test_periodic;
	timer.expires = jiffies + msecs_to_jiffies(GENL_TEST_HELLO_INTERVAL);
	add_timer(&timer);
#else
	timer_setup(&timer, genl_test_periodic, 0);
	mod_timer(&timer, jiffies + msecs_to_jiffies(GENL_TEST_HELLO_INTERVAL));
#endif

	return 0;

failure:
	/* Cleanup already registered families */
	for (i = 0; i < registered_count; i++) {
		if (genl_test_families[i].name) {
			genl_unregister_family(&genl_test_families[i]);
		}
	}
	/* Free all allocated names and groups */
	for (i = 0; i < GENL_TEST_FAMILY_COUNT; i++) {
		if (genl_test_families[i].name) {
			kfree(genl_test_families[i].name);
			genl_test_families[i].name = NULL;
		}
		genl_test_free_mcgrps(i);
	}
	printk(KERN_DEBUG "genl_test: error occurred in %s\n", __func__);
	return -EINVAL;
}
module_init(genl_test_init);

static void genl_test_exit(void)
{
	unsigned int i;
	int rc;

	del_timer(&timer);

	for (i = 0; i < GENL_TEST_FAMILY_COUNT; i++) {
		if (genl_test_families[i].name) {
			rc = genl_unregister_family(&genl_test_families[i]);
			if (rc)
				printk(KERN_WARNING "Failed to unregister family %u: %d\n", i, rc);
			kfree(genl_test_families[i].name);
			genl_test_families[i].name = NULL;
		}
		genl_test_free_mcgrps(i);
	}
}
module_exit(genl_test_exit);

MODULE_AUTHOR("Ahmed Zaki <anzaki@gmail.com>");
MODULE_LICENSE("GPL");
