//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

#ifdef LAB_MMAP
uint64
sys_mmap(void)
{
  uint64 addr;
  int length, prot, flags, fd;
  uint64 offset;
  struct file *f;
  struct proc *p = myproc();

  argaddr(0, &addr);
  argint(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argaddr(5, &offset);

  // Check arguments
  if(length <= 0 || (prot & ~(PROT_READ | PROT_WRITE)) != 0)
    return -1;
  
  if(fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;

  // Check permission consistency
  if(flags == MAP_SHARED) {
    if((prot & PROT_WRITE) && !f->writable)
      return -1;
    if((prot & PROT_READ) && !f->readable)
      return -1;
  }

  // Find an unused VMA slot
  int vma_idx = -1;
  for(int i = 0; i < NVMA; i++) {
    if(!p->vmas[i].used) {
      vma_idx = i;
      break;
    }
  }
  if(vma_idx == -1)
    return -1;

  // Find unused address space
  uint64 map_addr;
  if(addr == 0) {
    // Kernel chooses address - start from end of heap and work up
    map_addr = PGROUNDUP(p->sz);
    
    // Check if this conflicts with any existing VMAs
    int conflict;
    do {
      conflict = 0;
      for(int i = 0; i < NVMA; i++) {
        if(p->vmas[i].used) {
          uint64 vma_start = p->vmas[i].addr;
          uint64 vma_end = p->vmas[i].addr + p->vmas[i].length;
          uint64 map_end = map_addr + length;
          
          // Check if ranges overlap
          if(!(map_end <= vma_start || map_addr >= vma_end)) {
            conflict = 1;
            map_addr = vma_end; // Move past this VMA
            break;
          }
        }
      }
    } while(conflict);
    
    // Make sure we don't go too high in virtual memory
    if(map_addr + length >= MAXVA)
      return -1;
  } else {
    map_addr = addr;
  }

  // Set up VMA
  p->vmas[vma_idx].used = 1;
  p->vmas[vma_idx].addr = map_addr;
  p->vmas[vma_idx].length = length;
  p->vmas[vma_idx].prot = prot;
  p->vmas[vma_idx].flags = flags;
  p->vmas[vma_idx].file = f;
  p->vmas[vma_idx].offset = offset;

  // Increase file reference count
  filedup(f);

  return map_addr;
}

uint64
sys_munmap(void)
{
  uint64 addr;
  int length;
  
  argaddr(0, &addr);
  argint(1, &length);

  struct proc *p = myproc();

  // Find the VMA containing this address range
  for(int i = 0; i < NVMA; i++) {
    struct vma *v = &p->vmas[i];
    if(v->used && addr >= v->addr && addr + length <= v->addr + v->length) {
      // Handle partial unmapping
      if(addr == v->addr && length == v->length) {
        // Unmapping entire VMA
        
        // Write back MAP_SHARED pages that might be dirty
        if(v->flags == MAP_SHARED && (v->prot & PROT_WRITE)) {
          for(uint64 va = v->addr; va < v->addr + v->length; va += PGSIZE) {
            uint64 pa = walkaddr(p->pagetable, va);
            if(pa != 0) {
              // Write the page back to file
              uint64 offset_in_vma = va - v->addr;
              uint64 file_offset = v->offset + offset_in_vma;
              
              begin_op();
              ilock(v->file->ip);
              int bytes_to_write = PGSIZE;
              // Don't write beyond the file
              if(file_offset + PGSIZE > v->file->ip->size) {
                if(file_offset < v->file->ip->size) {
                  bytes_to_write = v->file->ip->size - file_offset;
                } else {
                  bytes_to_write = 0;
                }
              }
              if(bytes_to_write > 0) {
                writei(v->file->ip, 0, pa, file_offset, bytes_to_write);
              }
              iunlock(v->file->ip);
              end_op();
            }
          }
        }
        
        // Unmap all pages in this VMA
        uvmunmap_safe(p->pagetable, v->addr, v->length / PGSIZE, 1);
        
        // Close file
        fileclose(v->file);
        
        // Mark VMA as unused
        v->used = 0;
        return 0;
      } else {
        // Partial unmapping
        
        // Write back MAP_SHARED pages that might be dirty
        if(v->flags == MAP_SHARED && (v->prot & PROT_WRITE)) {
          for(uint64 va = addr; va < addr + length; va += PGSIZE) {
            uint64 pa = walkaddr(p->pagetable, va);
            if(pa != 0) {
              // Write the page back to file
              uint64 offset_in_vma = va - v->addr;
              uint64 file_offset = v->offset + offset_in_vma;
              
              begin_op();
              ilock(v->file->ip);
              int bytes_to_write = PGSIZE;
              // Don't write beyond the file
              if(file_offset + PGSIZE > v->file->ip->size) {
                if(file_offset < v->file->ip->size) {
                  bytes_to_write = v->file->ip->size - file_offset;
                } else {
                  bytes_to_write = 0;
                }
              }
              if(bytes_to_write > 0) {
                writei(v->file->ip, 0, pa, file_offset, bytes_to_write);
              }
              iunlock(v->file->ip);
              end_op();
            }
          }
        }
        
        // Unmap the specified pages
        uvmunmap_safe(p->pagetable, addr, length / PGSIZE, 1);
        
        // Update VMA to reflect partial unmapping
        if(addr == v->addr) {
          // Unmapping from the beginning
          v->addr += length;
          v->offset += length;
          v->length -= length;
        } else if(addr + length == v->addr + v->length) {
          // Unmapping from the end
          v->length -= length;
        } else {
          // Unmapping from the middle - would need to split VMA
          // For now, we don't support this case
          return -1;
        }
        
        // If VMA becomes empty, mark as unused
        if(v->length == 0) {
          fileclose(v->file);
          v->used = 0;
        }
        
        return 0;
      }
    }
  }
  
  return -1;
}
#endif
