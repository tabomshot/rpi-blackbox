
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "piSendEvent.hh"

void send_event_launched (void)
{
	int status;
    pid_t pid;
	
	if ((pid = fork ()) >= 0) {
		if (pid == 0) {
			execlp ("curl", "curl", server_launched_event_url, NULL);
		} else {
			// parent process
            // wait for the child process
            waitpid (pid, &status, 0);
		}
	} else {
		fprintf (stderr, "cannot create curl process...\n");
	}
}

void send_event_touched (void)
{
	int status;
    pid_t pid;
	
	if ((pid = fork ()) >= 0) {
		if (pid == 0) {
			execlp ("curl", "curl", button_touched_event_url, NULL);
		} else {
			// parent process
            // wait for the child process
            waitpid (pid, &status, 0);
		}
	} else {
		fprintf (stderr, "cannot create curl process...\n");
	}
}
