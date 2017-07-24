#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NUM_DIRECT_INDEX 122 
#define NUM_INDEX_PER_SEC ( BLOCK_SECTOR_SIZE / 4 ) 
#define LEVEL0_CAP ( NUM_DIRECT_INDEX*BLOCK_SECTOR_SIZE)
#define LEVEL1_CAP ( NUM_INDEX_PER_SEC*BLOCK_SECTOR_SIZE)
#define LEVEL2_CAP ( NUM_INDEX_PER_SEC*NUM_INDEX_PER_SEC*BLOCK_SECTOR_SIZE)

// static struct lock openInodeLock; 
// static struct hash openInodes;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
  
    // block_sector_t index0 [NUM_DIRECT_INDEX];
    // block_sector_t index1;
    // block_sector_t index2;
    // int isDirectory;

  };

//Proj4
struct indirectBlock {

  block_sector_t index [NUM_INDEX_PER_SEC];

};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    //struct hash_elem elem;
    struct lock inodeLock;
    struct lock directoryLock;
    bool isDirectory;
    off_t length;
  };

static bool extendInodeSize( struct inode_disk *inodeDisk, const off_t length ) {

  off_t extend = length - inodeDisk->length;
  off_t left = ROUND_UP( inodeDisk->length, BLOCK_SECTOR_SIZE) - inodeDisk->length;

  if ( extend <= left ) {

    inodeDisk->length = length;
    return true;

  } else {

    inodeDisk->length = ROUND_UP( inodeDisk->length, BLOCK_SECTOR_SIZE );

    // while ( inodeDisk->length < length ) {

    //   if ( !)

    // }

    inodeDisk->length = length;
    return true;

  }

}

static inline off_t directOffset( off_t offset ) {

  ASSERT( offset < LEVEL0_CAP );
  return offset/BLOCK_SECTOR_SIZE;

}

static inline off_t indirectOffset( off_t offset ) {

  ASSERT( offset >= LEVEL0_CAP );
  ASSERT( offset < LEVEL0_CAP + LEVEL1_CAP );
  return ( offset - LEVEL0_CAP )/BLOCK_SECTOR_SIZE;

}

static inline off_t doubleIndirOffset_1( off_t offset ) {

  ASSERT( offset >= LEVEL0_CAP + LEVEL1_CAP );
  ASSERT( offset < LEVEL0_CAP + LEVEL1_CAP + LEVEL2_CAP );
  return ( offset - LEVEL0_CAP - LEVEL1_CAP )/LEVEL1_CAP;

}

static inline off_t doubleIndirOffset_2( off_t offset ) {

  ASSERT( offset >= LEVEL0_CAP + LEVEL1_CAP );
  ASSERT( offset < LEVEL0_CAP + LEVEL1_CAP + LEVEL2_CAP );
  return ( offset - LEVEL0_CAP ) % LEVEL1_CAP / BLOCK_SECTOR_SIZE;  

}

static block_sector_t getIndirectSector( block_sector_t sec, off_t offset ) {

  struct indirectBlock *indirectBlock;
  indirectBlock = malloc( sizeof *indirectBlock );

  if ( indirectBlock == NULL ) {

    return -1;

  }

  //block_read (fs_device, sec, &inode->data);

  block_sector_t sector = indirectBlock->index[offset];

  free( indirectBlock );
  return sector;

}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{

  // if ( pos < LEVEL0_CAP ) {

  //   off_t offset = directOffset( pos );
  //   return inode->index0[offset];

  // } else if ( pos < (LEVEL0_CAP + LEVEL1_CAP) ) {

  //   off_t indirOffset = indirectOffset( pos );
  //   return getIndirectSector( inode->index1, indirOffset );

  // } else if ( pos < (LEVEL0_CAP + LEVEL1_CAP + LEVEL2_CAP) ) {

  //   off_t indirOffset = doubleIndirOffset_1( pos );
  //   off_t doubleIndirOffset = doubleIndirOffset_2( pos );

  //   return getIndirectSector( getIndirectSector(inode->index2, indirOffset), doubleIndirOffset );

  // }

  // return -1;
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

// static block_sector_t hashHelper( const struct hash_elem *hashElement, void *aux UNUSED ) {

//   struct inode *i  = hash_entry( hashElement, struct inode, elem );
//   return i->sector;

// }

// static bool compareHash( const struct hash_elem *elem1, const struct hash_elem *elem2, void *aux UNUSED ) {

//   struct inode *inode1 = hash_entry( elem1, struct inode, elem );
//   struct inode *inode2 = hash_entry( elem2, struct inode, elem );

//   return inode1->sector < inode2->sector;

// }

/* Initializes the inode module. */
void
inode_init (void) 
{
  //hash_init( &openInodes, hashHelper, compareHash, NULL );
  //lock_init( &openInodeLock );
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

   // if ( length < 0 ) {

  //   return false;

  // }

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);

  // if ( disk_inode == NULL ) {

  //   return false;

  // }

  // disk_inode->length = 0;
  // extendInodeSize( disk_inode, length );

  // ASSERT( disk_inode->length >= length );
  // ASSERT( disk_inode->length-length < BLOCK_SECTOR_SIZE );

  // disk_inode->start = sector;
  // disk_inode->magic = INODE_MAGIC;
  // disk_inode->isDirectory = isDir ? 1 : 0;

  // free( disk_inode );

  // return true;

  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  //struct hash_elem *hashElem;
  struct inode *inode;

  /* Check whether this inode is already open. */

  // struct inode temp;
  // temp.sector = sector;

  // lock_acquire( &openInodeLock );
  // hashElem = hash_find( &openInodes, &temp.elem );

  // if ( hashElem != NULL ) {

  //   lock_release( &openInodeLock );
  //   inode = hash_entry( hashElem, struct inode, elem );

  //   ASSERT( inode->sector == sector );

  //   inode_reopen( inode );

  //   return inode;

  //}


  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  // lock_init( &inode->inodeLock );
  // lock_init( &inode->directoryLock );
  // hash_insert( &openInodes, &inode->elem );
  // lock_release( &openInodeLock );

  block_read (fs_device, inode->sector, &inode->data);
  
  // struct inode_disk *inodeDisk;
  // inodeDisk = malloc( sizeof *inodeDisk );

  // if ( inodeDisk == NULL ) {

  //   return NULL;

  // }

  // inode->length = inodeDisk->length;
  // inode->isDirectory = inodeDisk->isDirectory != 0;

  // free( inodeDisk );


  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      // lock_acquire( &openInodeLock );
      // hash_delete( &openInodes, &inode->elem );
      // lock_release( &openInodeLock );

      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }
      //lock_release( &inode->inodeLock );
      free (inode); 
    }

    //lock_release( &inode->inodeLock );
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{

  // if ( offset >= inode->length ) {

  //   return 0;

  // }

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  // struct inode_disk *inodeDisk;
  // inodeDisk = malloc( sizeof *inodeDisk );

  // if ( inodeDisk == NULL ) {
  //   return 0;
  // }

  // lock_acquire( &inode->inodeLock );

  // bool extend = false;

  // if ( offset + size > inodeDisk->length ) {

  //   extend = true;

  //   if ( !extendInodeSize(inodeDisk, offset + size) ) {
  //     lock_release( &inode->inodeLock );
  //     free( inodeDisk );
  //     return 0;
  //   }

  // } else {

  //   lock_release( &inode->inodeLock );

  // }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  // if ( extend ) {

  //   lock_release( &inode->inodeLock );

  // }

  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

// void lockInode( struct inode *i ) {

//   lock_acquire( &i->inodeLock );

// }

// void unlockInode( struct inode *i ) {

//   lock_release( &i->inodeLock );

// }

// void lockDirectory( struct inode *i ) {

//   lock_acquire( &i->directoryLock );

// }

// void unlockDirectory( struct inode *i ) {

//   lock_release( &i->directoryLock );

// }

// bool isDirectory( struct inode *i ) {

//   if ( i != NULL ) {
//     if ( i->isDirectory == true ) {
//       return true;
//     }
//   }

//   return false;

// }

// int numOpenInodes( struct inode *i ) {

//   return i->open_cnt;

// }
