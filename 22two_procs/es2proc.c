#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

/*
Esercizio 3 – due processi

• Scrivere un programma che prende come parametro un nome di file da creare (nome_file)
• Legge i dati da stdin e li fa comprimere dall’utility zip nell’archivio compresso nome_file
• Quando l’utente digita Ctrl-C, il programma termina il processo zip, e
mostra la dimensione dell’archivio compresso (ovvero lancia il
comando ls -l nome_file.zip), poi termina.

• Esempio di comando bash che comprime la stringa ‘foo bar’
immagazzinandola in un un archivio compresso chiamato
nome_file.zip (l’estensione .zip viene aggiunta automaticamente)
echo 'foo bar' | zip -q nome_file -

 */

static int child_process_id = 0;
static char * nome_file;
static int pipefd[2];

void run_ls_zip();

void * final_thread(void * arg) {

	printf("[main final_thread]chiudo pipe estremità scrittura\n");
	if (close(pipefd[1]) == -1) { // chiude l'estremo di scrittura della pipe; il processo figlio "vede" EOF sull'estremità di lettura della pipe, quindi termina
		perror("close1 - final_thread");
	}

	printf("[main final_thread]prima di wait\n");
	if (wait(NULL) == -1) {
		perror("[main final_thread]wait");
	}

	// non torniamo da run_ls_zip
	run_ls_zip();

	return NULL;
}

void run_ls_zip() {

	char * newargv[] = { "ls", "-l", nome_file, NULL };

	char * newenviron[] = { NULL };

	printf("[main run_ls_zip]prima di execve\n");

	execve("/bin/ls", newargv, newenviron);

	perror("[main run_ls_zip] execve");

	exit(EXIT_FAILURE);
}


static void signal_handler(int sig) { // per parent process

	printf("[main signal_handler]ricevuto segnale %d, PID: %d\n", sig, getpid());

	if (sig == SIGINT) {

		pthread_t tid;
		// come esercizio, creiamo un thread
		if (pthread_create(&tid, NULL, final_thread, NULL) != 0)
			perror("pthread_create");
	}


}

void compressor(void) {
	// https://stackoverflow.com/questions/6108953/how-does-ctrl-c-terminate-a-child-process
	// https://stackoverflow.com/questions/31907212/will-ctrlc-send-sigint-signals-to-both-parent-and-child-processes-in-linux
	/*
	 * "The SIGINT signal is generated by the terminal line discipline, and broadcast to all processes in the terminal's foreground process group"
	 */

	pid_t pgid = getpgrp();

	printf("[child]prima di setpgrp(), pgid = %d\n", pgid);

	// cambia il process group; lo facciamo per evitare di ricevere SIGINT quando digitiamo Ctrl-C nel terminale
	setpgrp();

	pgid = getpgrp();

	printf("[child]dopo setpgrp(), pgid = %d\n", pgid);

	// se definisco un signal handler, questa impostazione verrà persa quando chiamerò execve
//	if (signal(SIGINT, ...) == SIG_ERR) {
//     ...
//	}

	if (close(pipefd[1]) == -1) { // non ci serve il lato pipe in scrittura
		perror("close3");
	}

	// chiudiamo stdin e sostituiamo con pipefd[0]
	if (dup2(pipefd[0], STDIN_FILENO) == -1) { //
		perror("dup2");
	}

	char * newargv[] = { "zip", "-q", nome_file, "-", NULL };

	char * newenviron[] = { NULL };

	printf("[child]prima di execve /usr/bin/zip (con pipefd[0] al posto di stdin)\n");

	// ora lanciamo zip, che al posto di stdin si troverà l'estremità di lettura della pipe (ma per zip non cambia nulla, pensa di leggere da stdin)
	execve("/usr/bin/zip", newargv, newenviron);

	perror("[child]se siamo qui, qualcosa è andato storto\n");

	exit(EXIT_FAILURE);
}

int main(int argc, char * argv[]) {

	if (argc == 1) {
		printf("parametro: nome di file da creare\n");
		exit(EXIT_FAILURE);
	}

	nome_file = argv[1];

	unlink(nome_file);

	// useremo una pipe per comunicare col processo figlio
	if (pipe(pipefd) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}

	printf("[main]parent PID: %d\n", getpid());

	switch (child_process_id = fork()) {
		case 0: // child process
			printf("[child]child PID: %d\n", getpid());

			compressor();

			break;
		case -1: // error
			perror("fork");

			exit(EXIT_FAILURE);

			break;
		default: // parent process
			;
	}

	// questa sezione viene eseguita soltanto dal parent process

	// signal handler
	if (signal(SIGINT, signal_handler) == SIG_ERR) {
		perror("signal");
		exit(EXIT_FAILURE);
	}

	if (close(pipefd[0]) == -1) { // non ci serve l'estremità di lettura della pipe
		perror("close1");
	}

	int ch;
	while ((ch = getchar()) != -1) {
		if (write(pipefd[1], &ch, 1) != 1) {
			printf("\n[main]esco da while a causa di write\n");
			break;
		}
	}

	if (ch == -1) {
		printf("\n[main]uscito da while a causa di EOF su stdin\n");
	}

	printf("[main]chiudo pipe, estremità di scrittura\n");
	if (close(pipefd[1]) == -1) {
		perror("close2");
	}

	printf("[main]prima di wait\n");
	if (wait(NULL) == -1) {
		perror("wait - main");
	}

	printf("[main]bye!\n");


	return 0;
}
