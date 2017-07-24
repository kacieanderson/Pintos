#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();

  char *fileName;
  if ( strcmp(name, "/") == false ) {
    return false;
  }

  // if ( !parse(name, &dir, &fileName) ) {

  //   return false;

  // }

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  free( fileName );

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  char *fileName;
  if ( strcmp(name, "/") == false ) {
    return false;
  }

  // if ( !parse(name, &dir, &fileName) ) {

  //   return false;

  // }

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  free( fileName );

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();

  char *fileName;
  if ( strcmp(name, "/") == false ) {
    return false;
  }

  // if ( !parse(name, &dir, &fileName)) {

  //   return false;

  // }

  if ( !strcmp(fileName, ".") || !strcmp(fileName, "..") ) {

    dir_close( dir );
    free( fileName );
    return false;

  }
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  free( fileName );

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");

  struct dir *directory = dir_open_root();

  dir_add( directory, ".", ROOT_DIR_SECTOR);
  dir_add( directory, "..", ROOT_DIR_SECTOR);

  dir_close( directory );

  free_map_close ();
  printf ("done.\n");
}

// bool parse(const char *filePath, struct dir **directory, char **fileName) {

//   *fileName = NULL;
//   *directory = NULL;
//   size_t length = strlen(filePath);

//   if ( length == 0 ) {

//     return false;

//   }

//   char *buffer = malloc ( length + 1 );
//   memcpy( buffer, filePath, length+1 );

//   char *tok;
//   char *pointer;
//   struct dir *currentDirectory;
//   bool isRoot = false;
//   char *start = buffer;
//   char *end = buffer + strlen (buffer)-1;

//   while ( end >= start && *end != '/') {

//     end--;

//   }

//   end++;

  
//   while ( *start == ' ') {

//     start++;

//   }

//   while ( *start == '/') {

//     start++;
//     isRoot = true;

//   }

//   if ( isRoot == true ) {

//     currentDirectory = dir_open_root();

//     if ( end == NULL ) {

//       dir_close( currentDirectory );
//       free( buffer );
//       return false;

//     }

//   } else {

//     currentDirectory = dir_open_current();

//   }

//   /* Open each directory in the path */
//   for ( tok = strtok_r(start, "/", &pointer); tok != end; tok = strtok_r (NULL, "/", &pointer) ) {
    
//     struct inode *i = NULL;

//     if ( dir_lookup (currentDirectory, tok, &i) == false ) {
     
//       dir_close(currentDirectory);
//       free(buffer);
//       return false;

//     }

//     dir_close(currentDirectory);
//     currentDirectory = dir_open(i);

//   }

//   *directory = currentDirectory;
//   size_t endLength = strlen(end) + 1;
//   *fileName = malloc(endLength);
//   memcpy(*fileName, end, endLength);
//   free(buffer);

//   return true;

// }
