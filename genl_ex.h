
#ifndef GENL_TEST_H
#define GENL_TEST_H

#include <linux/netlink.h>

#ifndef __KERNEL__
#include <stdio.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#endif

#define GENL_TEST_FAMILY_BASE_NAME	"genl_test"
#define GENL_TEST_FAMILY_COUNT		5
#define GENL_TEST_GROUPS_PER_FAMILY	100
#define GENL_TEST_TOTAL_GROUPS		(GENL_TEST_FAMILY_COUNT * GENL_TEST_GROUPS_PER_FAMILY)

#define GENL_TEST_ATTR_MSG_MAX		256
#define GENL_TEST_HELLO_INTERVAL	5000

enum {
	GENL_TEST_C_UNSPEC,		/* Must NOT use element 0 */
	GENL_TEST_C_MSG,
};

/* Helper macros to convert global group ID to family and local group */
#define GENL_TEST_GET_FAMILY_IDX(global_grp)	((global_grp) / GENL_TEST_GROUPS_PER_FAMILY)
#define GENL_TEST_GET_LOCAL_GRP(global_grp)	((global_grp) % GENL_TEST_GROUPS_PER_FAMILY)

#ifndef __KERNEL__
/* User-space helper functions */
static inline const char* genl_test_get_family_name(unsigned int family_idx)
{
	static char family_name_buf[32];
	snprintf(family_name_buf, sizeof(family_name_buf), "%s%u", GENL_TEST_FAMILY_BASE_NAME, family_idx);
	return family_name_buf;
}

static inline const char* genl_test_get_group_name(unsigned int group_idx)
{
	static char group_name_buf[32];
	snprintf(group_name_buf, sizeof(group_name_buf), "genl_mcgrp%u", group_idx);
	return group_name_buf;
}
#endif

enum genl_test_attrs {
	GENL_TEST_ATTR_UNSPEC,		/* Must NOT use element 0 */

	GENL_TEST_ATTR_MSG,

	__GENL_TEST_ATTR__MAX,
};
#define GENL_TEST_ATTR_MAX (__GENL_TEST_ATTR__MAX - 1)

static struct nla_policy genl_test_policy[GENL_TEST_ATTR_MAX+1] = {
	[GENL_TEST_ATTR_MSG] = {
		.type = NLA_STRING,
#ifdef __KERNEL__
		.len = GENL_TEST_ATTR_MSG_MAX
#else
		.maxlen = GENL_TEST_ATTR_MSG_MAX
#endif
	},
};

#endif
