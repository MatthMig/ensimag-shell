/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

struct process_background_linked {
	char *name;
	pid_t pid;
	struct timeval start_time;
	struct process_background_linked *next;
};

struct process_background_linked *process_background_head = NULL;
struct process_background_linked *process_background_tail = NULL;

void add_process_background(char *name, pid_t pid);
void remove_process_background(pid_t pid, struct timeval end_time);
void jobs();
void exec_cmdline(struct cmdline *l);
void remove_terminated_process_background();
struct timeval elapsed_time(struct timeval start, struct timeval end);
void modify_io(char *in, char *out);

#if USE_GUILE == 1
#include <libguile.h>

void add_process_background(char *name, pid_t pid) {
	if(name == NULL) {
		fprintf(stderr, "Error: command not valid\n");
		return;
	}
	struct process_background_linked *new_process = (struct process_background_linked*) malloc(sizeof(struct process_background_linked));
	if (new_process == NULL) {
		fprintf(stderr, "Error: memory allocation failed\n");
		return;
	}
	new_process->name = (char*) malloc(strlen(name) + 1);
	if (new_process->name == NULL) {
		fprintf(stderr, "Error: memory allocation failed\n");
		free(new_process);
		return;
	}
	strcpy(new_process->name, name);
	new_process->pid = pid;
	gettimeofday(&new_process->start_time, NULL);
	new_process->next = NULL;

	if(process_background_head == NULL) {
		process_background_head = new_process;
		process_background_tail = new_process;
	} else if (process_background_head == process_background_tail) {
		process_background_tail = new_process;
		process_background_head->next = process_background_tail;
	} else {
		process_background_tail->next = new_process;
		process_background_tail = new_process;
	}
}

void remove_process_background(pid_t pid, struct timeval end_time) {
	struct process_background_linked *current = process_background_head;
	struct process_background_linked *previous = NULL;
	struct timeval exec_time = elapsed_time(current->start_time, end_time);
	while(current != NULL) {
		if(current->pid == pid) {
			if(previous == NULL) {
				process_background_head = current->next;
			} else if (current->next == NULL) {
				process_background_tail = previous;
				previous->next = NULL;
			} else {
				previous->next = current->next;
			}
			printf("PID : %d\tCommand : %s\t\tExecution time : %d,%d sec\n", current->pid, current->name, (int) exec_time.tv_sec, (int) exec_time.tv_usec);
			free(current->name);
			free(current);
			return;
		}
		previous = current;
		current = current->next;
	}
	fprintf(stderr, "Error: process not found\n");
}

int is_background_process(pid_t pid) {
	struct process_background_linked *current = process_background_head;
	while(current != NULL) {
		if(current->pid == pid) {
			return 1;
		}
		current = current->next;
	}
	if (current == NULL) {
		return 0;
	}
	return 1;
}

void exec_cmdline(struct cmdline *l) {
	if(l->err) {
		fprintf(stderr, "Error: %s\n", l->err);
		return;
	
	}

	int pipe_len;
	for (pipe_len=0; l->seq[pipe_len]!=0; pipe_len++);
	int pipefd[2];
	int prev_pipefd[2];
	
	for (int i=0; l->seq[i]!=0; i++) {
		char **cmd = l->seq[i];
		if (i != pipe_len - 1) {
			pipe(pipefd);
		}
		pid_t pid = fork();
		switch (pid)
		{
			case -1:
				perror("fork");
				exit(1);
			case 0:
				modify_io(l->in, l->out);
				if (i != 0) {
					dup2(prev_pipefd[0], 0);
					close(prev_pipefd[0]);
					close(prev_pipefd[1]);
				}
				if (i != pipe_len - 1) {
					close(pipefd[0]);
					dup2(pipefd[1], 1);
					close(pipefd[1]);
				}
				break;
			default:
				if (i != 0) {
					close(prev_pipefd[0]);
					close(prev_pipefd[1]);
				}
				if (i != pipe_len - 1) {
					prev_pipefd[0] = pipefd[0];
					prev_pipefd[1] = pipefd[1];
				}
				if (!l->bg) {
					int status;
					waitpid(pid, &status, 0);
				} else {
					add_process_background(cmd[0], pid);
				}
				continue;
		}
		if (!strcmp(cmd[0], "jobs")) {
			jobs();
			exit(0);
		} else if (execvp(cmd[0], cmd) == -1) {
			fprintf(stderr, "Error: command not valid\n");
			exit(1);
		}
	}
}

struct timeval elapsed_time(struct timeval start, struct timeval end) {
	struct timeval result;

	if (end.tv_usec == 0) {
		gettimeofday(&end, NULL);
	}

	result.tv_sec = end.tv_sec - start.tv_sec;
	result.tv_usec = end.tv_usec - start.tv_usec;
	if(result.tv_usec < 0) {
		result.tv_sec--;
		result.tv_usec += 1000000;
	}
	return result;
}

void jobs() {
	struct process_background_linked *current = process_background_head;
	if (current == NULL) {
		printf("No background process\n");
		return;
	}
	while(current != NULL) {
		struct timeval exec_time = elapsed_time(current->start_time, (struct timeval) {0, 0});
		printf("PID : %d\tCommand : %s\t\tExecution time : %d,%d sec\n", current->pid, current->name, (int) exec_time.tv_sec, (int) exec_time.tv_usec);
		current = current->next;
	}
}

void sigchld_sigaction(int signum, siginfo_t *siginfo, void* context) {
	struct timeval end_time;
	gettimeofday(&end_time, NULL);
	if(is_background_process(siginfo->si_pid)) {
		printf("Process %d terminated\n", siginfo->si_pid);
		remove_process_background(siginfo->si_pid, end_time);
	}
}

void modify_io(char *in, char *out) {
	if(in != NULL) {
		int fd = open(in, O_RDONLY);
		if(fd < 0) {
			perror("open");
			exit(1);
		}
		dup2(fd, 0);
		close(fd);
	}
	if(out != NULL) {
		int fd = open(out, O_RDWR);
		if(fd < 0) {
			fd = open(out, O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			close(fd);
			fd = open(out, O_RDWR);
		}
		dup2(fd, 1);
		close(fd);
	}
}

int question6_executer(char *line)
{
	/* Question 6: Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */
	printf("Not implemented yet: can not execute %s\n", line);

	/* Remove this line when using parsecmd as it will free it */
	free(line);
	
	return 0;
}

SCM executer_wrapper(SCM x)
{
        return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif


void terminate(char *line) {
#if USE_GNU_READLINE == 1
	/* rl_clear_history() does not exist yet in centOS 6 */
	clear_history();
#endif
	if (line)
	  free(line);
	printf("exit\n");
	exit(0);
}


int main() {
        printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
        scm_init_guile();
        /* register "executer" function in scheme */
        scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

	struct sigaction sa;
	memset(&sa, '\0', sizeof(struct sigaction));
	sa.sa_sigaction = sigchld_sigaction;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGCHLD, &sa, NULL);

	while (1) {
		struct cmdline *l;
		char *line=0;
		int i, j;
		char *prompt = "ensishell>";

		/* Readline use some internal memory structure that
		   can not be cleaned at the end of the program. Thus
		   one memory leak per command seems unavoidable yet */
		line = readline(prompt);
		if (line == 0 || ! strncmp(line,"exit", 4)) {
			terminate(line);
		}

#if USE_GNU_READLINE == 1
		add_history(line);
#endif


#if USE_GUILE == 1
		/* The line is a scheme command */
		if (line[0] == '(') {
			char catchligne[strlen(line) + 256];
			sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
			scm_eval_string(scm_from_locale_string(catchligne));
			free(line);
                        continue;
                }
#endif

		/* parsecmd free line and set it up to 0 */
		l = parsecmd( & line);

		/* If input stream closed, normal termination */
		if (!l) {		  
			terminate(0);
		}
		

		
		if (l->err) {
			/* Syntax error, read another command */
			printf("error: %s\n", l->err);
			continue;
		}

		if (l->in) printf("in: %s\n", l->in);
		if (l->out) printf("out: %s\n", l->out);
		if (l->bg) printf("background (&)\n");

		/* Display each command of the pipe */
		for (i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i];
			printf("seq[%d]: ", i);
                        for (j=0; cmd[j]!=0; j++) {
                                printf("'%s' ", cmd[j]);
                        }
			printf("\n");
		}

		exec_cmdline(l);
	}
}
