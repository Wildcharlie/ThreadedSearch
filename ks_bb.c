///////////////////////////////////////////////////////
// Fix search keyword to find end of line and period //
///////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <semaphore.h>
///////////////
// Constants //
///////////////

#define KEYWORDSIZE 32
#define DIRPATHSIZE 128
#define FILESIZE 128
#define LINESIZE 1024
#define RESULTSIZE 2048
#define MAXLINESIZE 1024

// Global variables for the buffer
int max;
int items = 0;
char **buffer;
int use  = 0;
int fill = 0;

// Semaphores for the buffer
sem_t empty;
sem_t full;
sem_t mutex;


////////////////////////////////////////
// Struct for sending data to threads //
////////////////////////////////////////

struct my_msgbuf {
	char mtext[RESULTSIZE];
	char keyword[KEYWORDSIZE];
	char dir[DIRPATHSIZE];
	char file[FILESIZE];
};


//////////////////////////////////////////////////////
// Search Function									//
//	Searches the given txt file for the keyword and //
//	sends the matching line to the buffer           //
//////////////////////////////////////////////////////

void *search(void *param) {
	struct my_msgbuf *p = (struct my_msgbuf*)param;
	FILE *fread;
	int line = 1;
	char buffer2[LINESIZE];
	char temp[LINESIZE];
	char thing[FILESIZE + DIRPATHSIZE];
	sprintf(thing, "%s/%s", p->dir, p->file);
	char *q = NULL;
	char *saveptr;

	int test;

	fread = fopen( thing, "r");								// Loop through every line in the text and if it matches the
	if (fread != NULL) {									// keyword then it passes it as a message to the client
		while( fgets( buffer2, 512, fread ) != NULL ) {
			strcpy(temp,buffer2);
			q = temp;
			test = 0;
			while ((q=strtok_r(q, " .;,\r\n\0", &saveptr)) != NULL) {
				if((strcmp(q, p->keyword)) == 0) {
				sprintf(p->mtext, "%s:%d:%s", p->file, line, buffer2);
					sem_wait(&empty);
    				sem_wait(&mutex);
					strcpy(buffer[fill], p->mtext);
					fill++;
					if (fill == max) fill = 0;
					sem_post(&mutex);
					sem_post(&full);
					break;
				}
				q = NULL;
				test++;
			}
			line++;
		}
	}
	fclose(fread);
	sem_wait(&mutex);
	items--;

	// When all the threads are done end the printing thread
	if (items == 0) {
		sem_post(&mutex);
		sem_wait(&empty);
		sem_wait(&mutex);
		strcpy(buffer[fill], "exit!");
		fill++;
		if (fill == max) fill = 0;
		sem_post(&mutex);
		sem_post(&full);
	}
	else sem_post(&mutex);
	pthread_exit(0);
	return NULL;
}


/////////////////////
// Printing thread //
/////////////////////

void *consumer(void *arg) {
	while (items >= 0) {
		sem_wait(&full);
		sem_wait(&mutex);
		if (strcmp(buffer[use], "exit!") == 0) {
			sem_post(&mutex);
			sem_post(&empty);
			break;
		}
		printf("%s", buffer[use]);
		use++;
		if (use == max) use = 0;
		sem_post(&mutex);
		sem_post(&empty);
	}
	pthread_exit(0);
	return NULL;
}

//////////////////////////////////////////////////////
// Child Function								    //
//	When the main process forks, the child runs     //
//	this function, which goes through the given     //
//	directory and creates threads using the search  //
//	function to search through every txt file       //
//////////////////////////////////////////////////////

void child2(struct my_msgbuf buf) {
	DIR *dp;
	struct dirent *entry;
	dp = opendir(buf.dir);
	int count = 0;
	void *p = NULL;
	void *g = NULL;
	pthread_t *tid = g;
	struct my_msgbuf* param = p;

	struct my_msgbuf end;
	memset(&end, 0, sizeof(struct my_msgbuf));
	
	while ((entry = readdir(dp))) {																// Loop through the directory
		if ((strcmp(".", entry->d_name) == 0) || (strcmp("..", entry->d_name) == 0)) continue;	// store each file in its own
		else {																					// struct to be sent to the search
			count++;																			// function
			p = (struct my_msgbuf*)realloc(param, (count)*sizeof(struct my_msgbuf));
			param = p;
			g = (pthread_t*)realloc(tid, (count)*sizeof(pthread_t));
			tid = g;
			memset(&param[count-1], 0, sizeof(struct my_msgbuf));
			strcpy(param[count-1].keyword, buf.keyword);
			strcpy(param[count-1].file, entry->d_name);
			strcpy(param[count-1].dir, buf.dir);
		}
	}

	items = count;
	int x;
	for (x = 0; x < count; x++) {							// Create the threads and join them so the child waits for
		pthread_create(&tid[x], NULL, search, &param[x]);	// all threads to complete before exiting
	}
	closedir(dp);

	pthread_t pid;
	pthread_create(&pid, NULL, consumer, NULL);
	int i;
	for (i = 0; i < count; i++) {
		pthread_join(tid[i], NULL);
	}
	pthread_join(pid, NULL);
	free(param);
	free(tid);
}

void child(char *dirPath, char *keyword, int bufSize) {
	struct my_msgbuf mbuf;
	strcpy(mbuf.keyword, keyword);
	strcpy(mbuf.dir, dirPath);
	max = bufSize;
  	sem_init(&empty, 0, max); // max are empty 
  	sem_init(&full, 0, 0);    // 0 are full
  	sem_init(&mutex, 0, 1);   // mutex

	buffer = (char **) malloc(max*sizeof(char*));	// Create the buffer
	int i;
	for (i = 0; i<max; i++) buffer[i] = (char*)malloc(MAXLINESIZE);
	child2(mbuf);
	for (i = 0; i<max; i++) free(buffer[i]);
	free(buffer);
	_exit(0);
}

////////////////////////////////////////////////////////////////////
// Main 														  //
//	Get the buffer size and file containing the search directories//
//	then create child processes to search the files               //
////////////////////////////////////////////////////////////////////


int main(int argc, char *argv[]) {
	char commandFile[KEYWORDSIZE], bufTemp[DIRPATHSIZE];
	int bufSize;
	strcpy(commandFile, argv[1]);
	strcpy(bufTemp, argv[2]);
	bufSize = atoi(bufTemp);

	FILE *fread;
	char buffer2[MAXLINESIZE];
	char temp[MAXLINESIZE];
	char *q = NULL;
	char *saveptr;
	char dirPath[DIRPATHSIZE];
	char keyword[KEYWORDSIZE];
	int line;
	pid_t c1;
	int status;
	int n = 0;

	// Get each directory and search word and create a child for it
	fread = fopen(commandFile, "r");
	if (fread != NULL) {
		while( fgets(buffer2, MAXLINESIZE, fread) != NULL ) {
			strcpy(temp,buffer2);
			q = temp;
			line = 0;
			while ((q=strtok_r(q, " \r\n\0", &saveptr)) != NULL) {
				if (line == 0) strcpy(dirPath, q);
				else strcpy(keyword, q);
				q = NULL;
				line++;
			}
			c1 = fork(); // Create child
			n++;
			if (c1 == 0) {
				fclose(fread);
				child(dirPath, keyword, bufSize);
			}
		}
	}
	fclose(fread);

	while (n > 0) {	// Wait for all child processes
		wait(&status);
		n--;
	}

	return 0;

}
