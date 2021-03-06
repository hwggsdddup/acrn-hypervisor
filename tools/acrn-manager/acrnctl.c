/*
 * ProjectAcrn 
 * Acrnctl
 *
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *
 * Author: Tao Yuhong <yuhong.tao@intel.com>
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "acrn_mngr.h"
#include "acrnctl.h"

#define ACRNCTL_OPT_ROOT	"/opt/acrn/conf"

#define ACMD(CMD,FUNC,DESC, VALID_ARGS) \
{.cmd = CMD, .func = FUNC, .desc = DESC, .valid_args = VALID_ARGS}

/* vm life cycle cmd description */
#define LIST_DESC      "List all the virtual machines added"
#define START_DESC     "Start virtual machine VM_NAME"
#define STOP_DESC      "Stop virtual machine VM_NAME"
#define DEL_DESC       "Delete virtual machine VM_NAME"
#define ADD_DESC       "Add one virtual machine with SCRIPTS and OPTIONS"
#define PAUSE_DESC     "Block all vCPUs of virtual machine VM_NAME"
#define CONTINUE_DESC  "Start virtual machine from pause state"
#define SUSPEND_DESC   "Switch virtual machine to suspend state"
#define RESUME_DESC    "Resume virtual machine from suspend state"
#define RESET_DESC     "Stop and then start virtual machine VM_NAME"

struct acrnctl_cmd {
	const char *cmd;
	const char desc[128];	/* Description of the cmd */
	int (*func) (int argc, char *argv[]);
	/* Callback function to check whether args is valid */
	int (*valid_args) (struct acrnctl_cmd * cmd, int argc, char *argv[]);
};

/* There are acrnctl cmds */
/* command: list */
static int acrnctl_do_list(int argc, char *argv[])
{
	return list_vm();
}

static int check_name(const char *name)
{
	int i = 0, j = 0;
	char illegal[] = "!@#$%^&*, ";

	/* Name should start with a letter */
	if ((name[0] < 'a' || name[0] > 'z')
	    && (name[0] < 'A' || name[0] > 'Z')) {
		printf("name not started with latter!\n");
		return -1;
	}

	/* no illegal charactoer */
	while (name[i]) {
		j = 0;
		while (illegal[j]) {
			if (name[i] == illegal[j]) {
				printf("vmname[%d] is '%c'!\n", i, name[i]);
				return -1;
			}
			j++;
		}
		i++;
	}

	if (!strcmp(name, "help"))
		return -1;
	if (!strcmp(name, "nothing"))
		return -1;

	return 0;
}

static const char *acrnctl_bin_path;
static int find_acrn_dm;

static int write_tmp_file(int fd, int n, char *word[])
{
	int len, ret, i = 0;
	char buf[128];

	if (!n)
		return 0;

	len = strlen(word[0]);
	if (len >= strlen("acrn-dm")) {
		if (!strcmp(word[0] + len - strlen("acrn-dm"), "acrn-dm")) {
			find_acrn_dm++;
			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf), "%s gentmpfile",
				 acrnctl_bin_path);
			ret = write(fd, buf, strlen(buf));
			if (ret < 0)
				return -1;
			i++;
		}
	}

	while (i < n) {
		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), " %s", word[i]);
		i++;
		ret = write(fd, buf, strlen(buf));
		if (ret < 0)
			return -1;
	}
	ret = write(fd, "\n", 1);
	if (ret < 0)
		return -1;
	return 0;
}

#define MAX_FILE_SIZE   (4096 * 4)
#define MAX_WORD	64
#define FILE_NAME_LENGTH	128

#define TMP_FILE_SUFFIX		".acrnctl"

static int acrnctl_do_add(int argc, char *argv[])
{
	struct vmmngr_struct *s;
	int fd, fd_tmp, ret = 0;
	char *buf;
	char *word[MAX_WORD], *line;
	char *word_p = NULL, *line_p = NULL;
	int n_word;
	char fname[FILE_NAME_LENGTH + sizeof(TMP_FILE_SUFFIX)];
	char cmd[128];
	char args[128];
	int p, i;
	char cmd_out[256];
	char vmname[128];
	size_t len = sizeof(cmd_out);

	if (strlen(argv[1]) >= FILE_NAME_LENGTH) {
		printf("file name too long: %s\n", argv[1]);
		return -1;
	}

	memset(args, 0, sizeof(args));
	p = 0;
	for (i = 2; i < argc; i++) {
		if (p >= sizeof(args) - 1) {
			args[sizeof(args) - 1] = 0;
			printf("Too many optional args: %s\n", args);
			return -1;
		}
		p += snprintf(&args[p], sizeof(args) - p, " %s", argv[i]);
	}
	args[p] = ' ';

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror(argv[1]);
		ret = -1;
		goto open_read_file;
	}

	buf = calloc(1, MAX_FILE_SIZE);
	if (!buf) {
		perror("calloc for add vm");
		ret = -1;
		goto calloc_err;
	}

	ret = read(fd, buf, MAX_FILE_SIZE);
	if (ret >= MAX_FILE_SIZE) {
		printf("%s exceed MAX_FILE_SIZE:%d", argv[1], MAX_FILE_SIZE);
		ret = -1;
		goto file_exceed;
	}

	/* open tmp file for write */
	memset(fname, 0, sizeof(fname));
	snprintf(fname, sizeof(fname), "%s%s", argv[1], TMP_FILE_SUFFIX);
	fd_tmp = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd_tmp < 0) {
		perror(fname);
		ret = -1;
		goto open_tmp_file;
	}

	find_acrn_dm = 0;

	/* Properly null-terminate buf */
	buf[MAX_FILE_SIZE - 1] = '\0';

	line = strtok_r(buf, "\n", &line_p);
	while (line) {
		word_p = NULL;
		n_word = 0;
		word[n_word] = strtok_r(line, " ", &word_p);
		while (word[n_word]) {
			n_word++;
			word[n_word] = strtok_r(NULL, " ", &word_p);
		}
		if (write_tmp_file(fd_tmp, n_word, word)) {
			ret = -1;
			perror(fname);
			goto write_tmpfile;
		}
		line = strtok_r(NULL, "\n", &line_p);
	}

	if (!find_acrn_dm) {
		printf
		    ("Don't see 'acrn-dm' in %s, maybe it is in another script, "
		     "this is no supported for now\n", argv[1]);
		ret = -1;
		goto no_acrn_dm;
	}

	snprintf(cmd, sizeof(cmd), "mv %s %s.back", argv[1], argv[1]);
	system(cmd);

	snprintf(cmd, sizeof(cmd), "mv %s %s", fname, argv[1]);
	system(cmd);

	memset(vmname, 0, sizeof(vmname));
	snprintf(cmd, sizeof(cmd), "bash %s%s >./%s.result", argv[1],
		 args, argv[1]);
	ret = shell_cmd(cmd, cmd_out, sizeof(cmd_out));
	if (ret < 0)
		goto get_vmname;

	snprintf(cmd, sizeof(cmd), "grep -a \"acrnctl: \" ./%s.result",
		 argv[1]);
	ret = shell_cmd(cmd, cmd_out, sizeof(cmd_out));
	if (ret < 0)
		goto get_vmname;

	ret = sscanf(cmd_out, "acrnctl: %s", vmname);
	if (ret != 1) {
		ret = -1;
		snprintf(cmd, sizeof(cmd), "cat ./%s.result", argv[1]);
		shell_cmd(cmd, cmd_out, sizeof(cmd_out));

		/* Properly null-terminate cmd_out */
		cmd_out[len - 1] = '\0';

		printf("%s can't reach acrn-dm, "
		       "please try again when you make sure it can launch an UOS\n"
		       "result:\n%s\n", argv[1], cmd_out);
		goto get_vmname;
	}

	ret = check_name(vmname);
	if (ret) {
		printf("\"%s\" is a bad name, please select another name\n",
		       vmname);
		goto get_vmname;
	}

	snprintf(cmd, sizeof(cmd), "mkdir -p %s/add", ACRNCTL_OPT_ROOT);
	system(cmd);

	s = vmmngr_find(vmname);
	if (s) {
		printf("%s(%s) already exist, can't add %s%s\n",
		       vmname, state_str[s->state], argv[1], args);
		ret = -1;
		goto vm_exist;
	}

	snprintf(cmd, sizeof(cmd), "cp %s.back %s/add/%s.sh", argv[1],
		 ACRNCTL_OPT_ROOT, vmname);
	system(cmd);

	snprintf(cmd, sizeof(cmd), "echo %s >%s/add/%s.args", args,
		 ACRNCTL_OPT_ROOT, vmname);
	system(cmd);
	printf("%s added\n", vmname);

 vm_exist:
 get_vmname:
	snprintf(cmd, sizeof(cmd), "rm -f ./%s.result", argv[1]);
	system(cmd);

	snprintf(cmd, sizeof(cmd), "mv %s %s", argv[1], fname);
	system(cmd);

	snprintf(cmd, sizeof(cmd), "mv %s.back %s", argv[1], argv[1]);
	system(cmd);

 no_acrn_dm:
	snprintf(cmd, sizeof(cmd), "rm -f %s", fname);
	system(cmd);
 write_tmpfile:
	close(fd_tmp);
 open_tmp_file:
 file_exceed:
	free(buf);
 calloc_err:
	close(fd);
 open_read_file:
	return ret;
}

static int acrnctl_do_stop(int argc, char *argv[])
{
	struct vmmngr_struct *s;
	int i;

	for (i = 1; i < argc; i++) {
		s = vmmngr_find(argv[i]);
		if (!s) {
			printf("can't find %s\n", argv[i]);
			continue;
		}
		if (s->state == VM_CREATED) {
			printf("%s is already (%s)\n", argv[i],
			       state_str[s->state]);
			continue;
		}
		stop_vm(argv[i]);
	}

	return 0;
}

/* command: delete */
static int acrnctl_do_del(int argc, char *argv[])
{
	struct vmmngr_struct *s;
	int i;
	char cmd[128];

	for (i = 1; i < argc; i++) {
		s = vmmngr_find(argv[i]);
		if (!s) {
			printf("can't find %s\n", argv[i]);
			continue;
		}
		if (s->state != VM_CREATED) {
			printf("can't delete %s(%s)\n", argv[i],
			       state_str[s->state]);
			continue;
		}
		snprintf(cmd, sizeof(cmd), "rm -f %s/add/%s.sh",
			 ACRNCTL_OPT_ROOT, argv[i]);
		system(cmd);
		snprintf(cmd, sizeof(cmd), "rm -f %s/add/%s.args",
			 ACRNCTL_OPT_ROOT, argv[i]);
		system(cmd);
	}

	return 0;
}

static int acrnctl_do_start(int argc, char *argv[])
{
	struct vmmngr_struct *s;

	s = vmmngr_find(argv[1]);
	if (!s) {
		printf("can't find %s\n", argv[1]);
		return -1;
	}

	if (s->state != VM_CREATED) {
		printf("can't start %s(%s)\n", argv[1], state_str[s->state]);
		return -1;
	}

	start_vm(argv[1]);

	return 0;
}

static int acrnctl_do_pause(int argc, char *argv[])
{
	struct vmmngr_struct *s;
	int i;

	for (i = 1; i < argc; i++) {
		s = vmmngr_find(argv[i]);
		if (!s) {
			printf("Can't find vm %s\n", argv[i]);
			continue;
		}

		/* Send pause cmd to arcn-dm only when vm is in VM_STARTED */
		switch (s->state) {
			case VM_STARTED:
				pause_vm(argv[i]);
				break;
			default:
				printf("%s current state %s, can't pause\n",
					argv[i], state_str[s->state]);
		}
	}

	return 0;
}

static int acrnctl_do_continue(int argc, char *argv[])
{
	struct vmmngr_struct *s;
	int i;

	for (i = 1; i < argc; i++) {
		s = vmmngr_find(argv[i]);
		if (!s) {
			printf("Can't find vm %s\n", argv[i]);
			continue;
		}

		/* Per current implemention, we can't know if vm is in paused
		   state. Send continue cmd to acrn-dm when VM_STARTED and will
		   correct it later when we have a way to check if vm has been
		   paused */
		switch (s->state) {
			case VM_STARTED:
				continue_vm(argv[i]);
				break;
			default:
				printf("%s current state %s, can't continue\n",
					argv[i], state_str[s->state]);
		}
	}

	return 0;
}

static int acrnctl_do_suspend(int argc, char *argv[])
{
	struct vmmngr_struct *s;
	int i;

	for (i = 1; i < argc; i++) {
		s = vmmngr_find(argv[1]);
		if (!s) {
			printf("Can't find vm %s\n", argv[i]);
			continue;
		}

		/* Only send suspend cmd to acrn-dm now when VM_STARTED */
		switch (s->state) {
			case VM_STARTED:
				suspend_vm(argv[i]);
				break;
			default:
				printf("%s current state %s, can't suspend\n",
					argv[i], state_str[s->state]);
		}
	}

	return 0;
}

static int acrnctl_do_resume(int argc, char *argv[])
{
	struct vmmngr_struct *s;
	int i;

	for (i = 1; i < argc; i++) {
		s = vmmngr_find(argv[i]);
		if (!s) {
			printf("Can't find vm %s\n", argv[i]);
			continue;
		}

		switch (s->state) {
			case VM_PAUSED:
				resume_vm(argv[i]);
				break;
			default:
				printf("%s current state %s, can't resume\n",
					argv[i], state_str[s->state]);
		}
	}

	return 0;
}

static int acrnctl_do_reset(int argc, char *argv[])
{
	struct vmmngr_struct *s;
	int i;

	for (i = 1; i < argc; i++) {
		s = vmmngr_find(argv[i]);
		if (!s) {
			printf("Can't find vm %s\n", argv[i]);
			continue;
		}

		switch(s->state) {
			case VM_CREATED:
				start_vm(argv[i]);
				break;
			case VM_STARTED:
			case VM_PAUSED:
				stop_vm(argv[i]);
				start_vm(argv[i]);
				break;
			default:
				printf("%s current state: %s, can't reset\n",
					argv[i], state_str[s->state]);
		}
	}
	return 0;
}

/* Default args validation function */
int df_valid_args(struct acrnctl_cmd *cmd, int argc, char *argv[])
{
	char df_opt[32] = "VM_NAME VM_NAME ...";

	if (argc < 2 || !strcmp(argv[1], "help")) {
		printf("acrnctl %s %s\n", cmd->cmd, df_opt);
		return -1;
	}

	return 0;
}

static int valid_add_args(struct acrnctl_cmd *cmd, int argc, char *argv[])
{
	char df_opt[32] = "launch_scripts options";

	if (argc < 2 || !strcmp(argv[1], "help")) {
		printf("acrnctl %s %s\n", cmd->cmd, df_opt);
		return -1;
	}

	return 0;
}

static int valid_start_args(struct acrnctl_cmd *cmd, int argc, char *argv[])
{
	char df_opt[16] = "VM_NAME";

	if (argc != 2 || ((argv + 1) && !strcmp(argv[1], "help"))) {
		printf("acrnctl %s %s\n", cmd->cmd, df_opt);
		return -1;
	}

	return 0;
}

static int valid_list_args(struct acrnctl_cmd *cmd, int argc, char *argv[])
{
	if (argc != 1) {
		printf("acrnctl %s\n", cmd->cmd);
		return -1;
	}

	return 0;
}

struct acrnctl_cmd acmds[] = {
	ACMD("list", acrnctl_do_list, LIST_DESC, valid_list_args),
	ACMD("start", acrnctl_do_start, START_DESC, valid_start_args),
	ACMD("stop", acrnctl_do_stop, STOP_DESC, df_valid_args),
	ACMD("del", acrnctl_do_del, DEL_DESC, df_valid_args),
	ACMD("add", acrnctl_do_add, ADD_DESC, valid_add_args),
	ACMD("pause", acrnctl_do_pause, PAUSE_DESC, df_valid_args),
	ACMD("continue", acrnctl_do_continue, CONTINUE_DESC, df_valid_args),
	ACMD("suspend", acrnctl_do_suspend, SUSPEND_DESC, df_valid_args),
	ACMD("resume", acrnctl_do_resume, RESUME_DESC, df_valid_args),
	ACMD("reset", acrnctl_do_reset, RESET_DESC, df_valid_args),
};

#define NCMD	(sizeof(acmds)/sizeof(struct acrnctl_cmd))

static void usage(void)
{
	int i;

	printf("\nUsage: acrnctl SUB-CMD "
		"{ VM_NAME | SCRIPTS OPTIONS | help }\n\n");
	for (i = 0; i < NCMD; i++)
		printf("\t%-12s%s\n", acmds[i].cmd, acmds[i].desc);
}

int main(int argc, char *argv[])
{
	int i, err;

	if (argc == 1 || !strcmp(argv[1], "help")) {
		usage();
		return 0;
	}

	acrnctl_bin_path = argv[0];

	/* first check acrnctl reserved operations */
	if (!strcmp(argv[1], "gentmpfile")) {
		printf("\nacrnctl: %s\n", argv[argc - 1]);
		return 0;
	}

	for (i = 0; i < NCMD; i++)
		if (!strcmp(argv[1], acmds[i].cmd)) {
			if (acmds[i].valid_args(&acmds[i], argc - 1, &argv[1])) {
				return -1;
			} else {
				vmmngr_update();
				err = acmds[i].func(argc - 1, &argv[1]);
				return err;
			}
		}

	/* Reach here means unsupported command */
	printf("Unknown command: %s\n", argv[1]);
	usage();

	return -1;
}
