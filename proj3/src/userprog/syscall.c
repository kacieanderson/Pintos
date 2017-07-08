#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "list.h"
#include "process.h"

extern bool running;

static void syscall_handler (struct intr_frame *);

void* check( const void* );
struct processFile* traverse( struct list* files, int fd );

struct processFile { // A struct that holds info about a file the current thread is associated with

	struct file *point; // pointer variable to the file in question
	int fd; // file descriptor for said file
	struct list_elem elem; // list element to keep list of processFiles

};

int executeProcess ( char *fileName ) {

	acquireFilesysLock();

	char * fnC = malloc( strlen(fileName) + 1 );
	strlcpy( fnC, fileName, strlen(fileName) + 1 );

	char * tempPointer;
	fnC = strtok_r( fnC, " ", &tempPointer );

	struct file* f = filesys_open(fnC);

	if ( f == NULL ) {

		releaseFilesysLock();
		return -1;

	} else {

		file_close(f);
		releaseFilesysLock();

		return process_execute(fileName);

	}

}

void exitProcess ( int exitStatus ) {

	struct list_elem *l;
	struct thread *t = thread_current();

	for ( l = list_begin(&t->parent->childProcesses); l != list_end(&t->parent->childProcesses); l = list_next(l) ) {

		struct child *c = list_entry( l, struct child, elem );

		if ( (c->tid) == (t->tid) ) {

			c->active = true;
			c->exitErr = exitStatus;

		}

	}

	t->exitErr = exitStatus;

	if( (t->parent->processWaitingFor) == (t->tid) ) {

		sema_up( &t->parent->childLock );

	}

	thread_exit();

}

void* check ( const void *vaddr ) {

	if ( is_user_vaddr(vaddr) == false ) {

		exitProcess(-1);
		return 0;

	}

	void *tempPointer = pagedir_get_page( thread_current()->pagedir, vaddr );

	if ( !tempPointer ) {

		exitProcess(-1);
		return 0;

	}

	return tempPointer;

}

struct processFile* traverse ( struct list* files, int fd ) {

	struct list_elem *l;

	for ( l = list_begin(files); l != list_end(files); l = list_next(l) ) {
		
		struct processFile *pf = list_entry( l, struct processFile, elem );

		if ( (pf->fd) == fd ) {
			return pf;
		}

	}

	return NULL;

}

void closeFile ( struct list* files, int fd ) {

	struct processFile *pf;
	struct list_elem *l;

	for ( l = list_begin(files); l != list_end(files); l = list_next(l) ) {
		
		pf = list_entry( l, struct processFile, elem );

		if ( (pf->fd) == fd ) {
			file_close(pf->point);
			list_remove(l);
		}

	}

	free(pf);

}

void closeAllFiles ( struct list* files ) {

	struct list_elem *l;

	while ( !list_empty(files) ) {

		l = list_pop_front(files);

		struct processFile *pf = list_entry( l, struct processFile, elem );

		file_close( pf->point );
		list_remove(l);

		free(pf);

	}

}

void
syscall_init (void) 
{

  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{

  int *i = f->esp;

  check(i);

  int systemCall = * i;

  switch (systemCall) {

  	case SYS_HALT:
  		shutdown_power_off();
  		break;

  	case SYS_EXIT:
  		check( i + 1 );

  		exitProcess( *(i + 1) );
  		break;

  	case SYS_EXEC:
  		check( i + 1 );
  		check( *(i + 1) );

  		f->eax = executeProcess( *(i + 1) );
  		break;

  	case SYS_WAIT:
  		check( i + 1 );

  		f->eax = process_wait( *(i + 1) );
  		break;

  	case SYS_CREATE:
  		check( i + 5 );
  		check( *(i + 4) );

  		acquireFilesysLock();
  		f->eax = filesys_create( *(i + 4), *(i + 5));
  		releaseFilesysLock();

  		break;

  	case SYS_REMOVE:
  		check( i + 1 );
  		check( *(i + 1) );

  		acquireFilesysLock();
  		if ( filesys_remove( *(i + 1) ) == NULL ) {
  			f->eax = false;
  		} else {
  			f->eax = true;
  		}
  		releaseFilesysLock();

  		break;

  	case SYS_OPEN:
  		check( i + 1 );
  		check( *(i + 1) );

  		acquireFilesysLock();
  		struct file* filePointer = filesys_open( *(i + 1) );
  		releaseFilesysLock();

  		if ( filePointer == NULL ) {
  			f->eax = -1;
  		} else {

  			struct processFile *pf = malloc(sizeof(*pf));
  			pf->point = filePointer;

  			pf->fd = thread_current()->fdNum;
  			thread_current()->fdNum++;

  			list_push_back(&thread_current()->files, &pf->elem);

  			f->eax = pf->fd;

  		}

  		break;

  	case SYS_FILESIZE:
  		check( i + 1 );

  		acquireFilesysLock();
  		f->eax = file_length( traverse(&thread_current()->files, *(i + 1))->point );
  		releaseFilesysLock();

  		break;

  	case SYS_READ:
  		check( i + 7 );
  		check( *(i + 6) ); 

  		if ( *(i + 5) == 0 ) {

  			int t;
  			uint8_t* buff = *(i + 6);

  			for ( t = 0; t < *(i + 7); t++ ) {
  				buff[t] = input_getc();
  			}

  			f->eax = *(i + 7);

  		} else {

  			struct processFile* filePointer = traverse( &thread_current()->files, *(i + 5) );

  			if ( filePointer == NULL ) {
  				f->eax = -1;
  			} else {

  				acquireFilesysLock();
  				f->eax = file_read( filePointer->point, *(i + 6), *(i + 7) );
  				releaseFilesysLock();

  			}
  		}

  		break;

  	case SYS_WRITE:
  		check( i + 7 );
  		check( *(i + 6) );

  		if ( *(i + 5) == 1 ) {

  			putbuf( *(i + 6), *(i + 7) );
  			f->eax = *(i + 7);

  		} else {

  			struct processFile* filePointer = traverse( &thread_current()->files, *(i + 5) );

  			if ( filePointer == NULL ) {
  				f->eax = -1;
  			} else {

  				acquireFilesysLock();
  				f->eax = file_write(filePointer->point, *(i + 6), *(i + 7));
  				releaseFilesysLock();

  			}
  		}

  		break;

  	case SYS_SEEK:
  		check( i + 5 );

  		acquireFilesysLock();
  		file_seek( traverse(&thread_current()->files, *(i + 4))->point, *(i + 5) );
  		releaseFilesysLock();

  		break;

  	case SYS_TELL:
  		check( i + 1 );

  		acquireFilesysLock();
  		f->eax = file_tell( traverse(&thread_current()->files, *(i + 1))->point );
  		releaseFilesysLock();

  		break;

  	case SYS_CLOSE:
  		check( i + 1 );

  		acquireFilesysLock();
  		closeFile( &thread_current()->files, *(i + 1) ); 
  		releaseFilesysLock();

  		break;

  	default:
  		printf("default %d\n", *i);
  }

}