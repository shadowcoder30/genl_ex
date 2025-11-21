#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netlink/msg.h>
#include <netlink/attr.h>

#include "genl_ex.h"

static char message[GENL_TEST_ATTR_MSG_MAX];
static int send_to_kernel;
static unsigned char mcgroups[GENL_TEST_TOTAL_GROUPS];	/* Array of group flags */

static void usage(char* name)
{
	printf("Usage: %s\n"
		"	-h : this help message\n"
		"	-l : listen on one or more groups, comma separated\n"
		"	-m : the message to send, required with -s\n"
		"	-s : send to kernel\n"
		"NOTE: specify either l or s, not both\n",
		name);
}

static void add_group(char* group)
{
	unsigned int grp = strtoul(group, NULL, 10);

	if (grp >= GENL_TEST_TOTAL_GROUPS) {
		fprintf(stderr, "Invalid group number %u. Values allowed 0:%u\n",
			grp, GENL_TEST_TOTAL_GROUPS - 1);
		exit(EXIT_FAILURE);
	}

	mcgroups[grp] = 1;
}

static void parse_cmd_line(int argc, char** argv)
{
	char* opt_val;

	while (1) {
		int opt = getopt(argc, argv, "hl:m:s");

		if (opt == EOF)
			break;

		switch (opt) {
		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);

		case 'l':
			opt_val = strtok(optarg, ",");
			add_group(opt_val); 
			while ((opt_val = strtok(NULL, ",")))
				add_group(opt_val); 

			break;

		case 'm':
			strncpy(message, optarg, GENL_TEST_ATTR_MSG_MAX);
			message[GENL_TEST_ATTR_MSG_MAX - 1] = '\0';
			break;

		case 's':
			send_to_kernel = 1;
			break;

		default:
			fprintf(stderr, "Unkown option %c !!\n", opt);
			exit(EXIT_FAILURE);
		}

	}

	/* sanity checks */
	/* Check if any groups are set */
	int has_groups = 0;
	for (unsigned int i = 0; i < GENL_TEST_TOTAL_GROUPS; i++) {
		if (mcgroups[i]) {
			has_groups = 1;
			break;
		}
	}

	if (send_to_kernel && has_groups)  {
		fprintf(stderr, "I can either receive or send messages.\n\n");
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}	
	if (!send_to_kernel && !has_groups)  {
		fprintf(stderr, "Nothing to do!\n");
		usage(argv[0]);
		exit(EXIT_SUCCESS);
	}
	if (send_to_kernel && message[0]=='\0') {
		fprintf(stderr, "What is the message you want to send?\n");
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}
}
static int send_msg_to_kernel(struct nl_sock *sock)
{
	struct nl_msg* msg;
	int family_id, err = 0;
	const char* family_name = genl_test_get_family_name(0); /* Use first family for sending */

	family_id = genl_ctrl_resolve(sock, family_name);
	if(family_id < 0){
		fprintf(stderr, "Unable to resolve family name %s!\n", family_name);
		exit(EXIT_FAILURE);
	}

	msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "failed to allocate netlink message\n");
		exit(EXIT_FAILURE);
	}

	if(!genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family_id, 0, 
		NLM_F_REQUEST, GENL_TEST_C_MSG, 0)) {
		fprintf(stderr, "failed to put nl hdr!\n");
		err = -ENOMEM;
		goto out;
	}

	err = nla_put_string(msg, GENL_TEST_ATTR_MSG, message);
	if (err) {
		fprintf(stderr, "failed to put nl string!\n");
		goto out;
	}

	err = nl_send_auto(sock, msg);
	if (err < 0) {
		fprintf(stderr, "failed to send nl message!\n");
	}

out:
	nlmsg_free(msg);
	return err;
}

static int skip_seq_check(struct nl_msg *msg, void *arg)
{
	return NL_OK;
}

static int print_rx_msg(struct nl_msg *msg, void* arg)
{
	struct nlattr *attr[GENL_TEST_ATTR_MAX+1];

	genlmsg_parse(nlmsg_hdr(msg), 0, attr, 
			GENL_TEST_ATTR_MAX, genl_test_policy);

	if (!attr[GENL_TEST_ATTR_MSG]) {
		fprintf(stdout, "Kernel sent empty message!!\n");
		return NL_OK;
	}

	fprintf(stdout, "Kernel says: %s \n", 
		nla_get_string(attr[GENL_TEST_ATTR_MSG]));

	return NL_OK;
}

static void prep_nl_sock(struct nl_sock** nlsock)
{
	int family_id, grp_id;
	
	*nlsock = nl_socket_alloc();
	if(!*nlsock) {
		fprintf(stderr, "Unable to alloc nl socket!\n");
		exit(EXIT_FAILURE);
	}

	/* disable seq checks on multicast sockets */
	nl_socket_disable_seq_check(*nlsock);
	nl_socket_disable_auto_ack(*nlsock);

	/* connect to genl */
	if (genl_connect(*nlsock)) {
		fprintf(stderr, "Unable to connect to genl!\n");
		goto exit_err;
	}

	/* Check if any groups are set */
	int has_groups = 0;
	for (unsigned int i = 0; i < GENL_TEST_TOTAL_GROUPS; i++) {
		if (mcgroups[i])
			has_groups = 1;
	}
	if (!has_groups)
		return;

	/* Join all requested groups */
	for (unsigned int global_grp = 0; global_grp < GENL_TEST_TOTAL_GROUPS; global_grp++) {
		if (!mcgroups[global_grp])
			continue;

		unsigned int family_idx = GENL_TEST_GET_FAMILY_IDX(global_grp);
		unsigned int local_grp = GENL_TEST_GET_LOCAL_GRP(global_grp);
		const char* family_name = genl_test_get_family_name(family_idx);
		const char* group_name = genl_test_get_group_name(local_grp);

		/* Resolve family if not already resolved for this family */
		family_id = genl_ctrl_resolve(*nlsock, family_name);
		if(family_id < 0){
			fprintf(stderr, "Unable to resolve family name %s for group %u!\n",
				family_name, global_grp);
			goto exit_err;
		}

		grp_id = genl_ctrl_resolve_grp(*nlsock, family_name, group_name);
		if (grp_id < 0)	{
			fprintf(stderr, "Unable to resolve group name %s (global group %u)!\n",
				group_name, global_grp);
			goto exit_err;
		}
		if (nl_socket_add_membership(*nlsock, grp_id)) {
			fprintf(stderr, "Unable to join group %u (family %s, group %s)!\n",
				global_grp, family_name, group_name);
			goto exit_err;
		}
	}

    return;

exit_err:
    nl_socket_free(*nlsock); // this call closes the socket as well
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
	struct nl_sock* nlsock = NULL;
	struct nl_cb *cb = NULL;
	int ret;

	parse_cmd_line(argc, argv);

	prep_nl_sock(&nlsock);

	if (send_to_kernel) {
		ret = send_msg_to_kernel(nlsock);
		exit(EXIT_SUCCESS);
	}

	/* prep the cb */
	cb = nl_cb_alloc(NL_CB_DEFAULT);
	nl_cb_set(cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, skip_seq_check, NULL);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, print_rx_msg, NULL);
	do {
		ret = nl_recvmsgs(nlsock, cb);
	} while (!ret);
	
	nl_cb_put(cb);
    nl_socket_free(nlsock);
	return 0;
}
