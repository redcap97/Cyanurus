/*
Copyright 2014 Akira Midorikawa

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef _CYANURUS_FS_DENTRY_H_
#define _CYANURUS_FS_DENTRY_H_

#include "inode.h"
#include "minix.h"
#include "lib/list.h"

#define PATH_MAX 1024
#define DF_FLAGS_LOAD (1 << 0)

struct dentry {
  char name[NAME_MAX];
  uint32_t flags;
  uint32_t nentries;
  struct inode *inode;
  struct dentry *parent;
  struct list children;
  struct list sibling;
};

void dentry_init(void);
struct dentry *dentry_lookup(const char *path);
int dentry_link(struct dentry *dentry, const char *path, struct inode *inode);
int dentry_unlink(struct dentry *dentry);

#endif
