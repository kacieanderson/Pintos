#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "list.h"
#include "process.h"

#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include "lib/string.h"
#include "lib/user/syscall.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

extern bool running;

static void syscall_handler (struct intr_frame *);

void* check( const void* );
struct processFile* traverse( struct list* files, int fd );

struct processFile { // A struct that holds info about a file the current thread is associated with

	struct file *point; // pointer variable to the file in question
	int fd; // file descriptor for said file
	struct list_elem elem; // list element to keep list of processFiles

};

// static bool chDir( const char *name ) {

//   struct thread *temp = thread_current();

//   if ( strcmp(name, "/") == false ) {

//     temp->currentDir = dir_open_root();
//     return true;

//   }

//   struct dir *directory;
//   char *directoryName;

//   if( parse(name, directory, directoryName) == false ) {

//     return false;

//   }

//   struct inode *i = NULL;

//   if ( dir_lookup(directory, directoryName, &i) == false ) {

//     dir_close( directory );
//     free( directoryName );
//     return false;

//   } 

//   dir_close( directory );
//   free( directoryName );
//   dir_close( temp->currentDir );
//   temp->currentDir = dir_open( i );
//   return true;

// }

// static bool mkDir( const char *name ) {

//   bool result = false;

//   if ( strcmp(name, "") == false || strcmp(name, "/") == false ) {
    
//     return false;
  
//   }

//   block_sector_t inodeSec = 0;
//   struct dir *directory;
//   struct dir *newDirectory;
//   char *directoryName;
  
//   if ( parse( name, &directory, &directoryName) == false ) {

//     return false;

//   }

//   if (! (directory != NULL && free_map_allocate(1, &inodeSec))) {

//     dir_close (directory);
//     free (directoryName);
//     return false;

//   }

 
//   if ( inode_create(inodeSec, 0, true) ) {
    
//     result = dir_add(directory, directoryName, inodeSec, true);
    
//     if (result == true) {

//       newDirectory  = dir_open( inode_open(inodeSec) );
//       result &= dir_add( newDirectory, ".", inodeSec, true);
      
//       if (result) {
//         result &= dir_add (newDirectory, "..", inode_get_inumber(dir_get_inode(directory)), true );
//       }

//       dir_close (newDirectory);

//       if ( result == false ) {
//         dir_remove (directory, directoryName);
//       }

//     }

//   }

//   dir_close (directory);
//   free (directoryName);
//   return result;

// }

// static bool readDir( int f, char name[15] ) {

//   struct thread *temp  = thread_current();
//   struct file *file = temp->me;
//   off_t position = file_tell(file);
//   struct inode *inode = file_get_inode(file);

//   if ( isDirectory(inode) ) {
//     return false;
//   }

//   struct dir *directory = dir_open(inode);
//   setPosition( directory, position );

//   bool result = dir_readdir( directory, name );

//   file_seek( file, getPosition(directory) );

//   free(directory);
//   return result;

// }

// static bool isDir( int f ) {

//   struct thread *temp  = thread_current();
//   struct file *file = temp->me;

//   bool result = (isDirectory(file_get_inode(file)));
//   return result;

// }

// static int iNumber( int f ) {

//   struct thread *temp  = thread_current();
//   struct file *file = temp->me;

//   int result = (inode_get_inumber(file_get_inode(file)));
//   return result;

// }

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

    // case SYS_CHDIR:
    //   check( i + 1 );

    //   acquireFilesysLock();
    //   f->eax = (uint32_t) chDir( (const char*) *(i + 1));
    //   releaseFilesysLock();

    // break;

    // case SYS_MKDIR:
    //   check( i + 1 );

    //   acquireFilesysLock();
    //   f->eax = (uint32_t) mkDir( (const char*) *(i + 1));
    //   releaseFilesysLock();

    //   break;

    // case SYS_READDIR:
    //   check( i + 1 );
    //   check( i + 2 );

    //   acquireFilesysLock();
    //   f->eax = (uint32_t) readDir( (int) *(i + 1), (char*) *(i + 2) );
    //   releaseFilesysLock();

    //   break;

    // case SYS_ISDIR:
    //   check( i + 1 );

    //   acquireFilesysLock();
    //   f->eax = (uint32_t) isDir( (int) *(i + 1));
    //   releaseFilesysLock();

    //   break;

    // case SYS_INUMBER:
    //   check( i + 1 );

    //   acquireFilesysLock();
    //   f->eax = (uint32_t) iNumber( (int) *(i + 1));
    //   releaseFilesysLock();

    //   break;

  	default:
  		printf("default %d\n", *i);
  }

}