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

#include "mmu.h"
#include "process.h"
#include "buddy.h"
#include "system.h"
#include "slab.h"
#include "lib/string.h"
#include "lib/list.h"

#define L1_ENTRY_NUM 4096
#define L2_ENTRY_NUM 256

#define L1_SIZE (L1_ENTRY_NUM * 4)
#define L2_SIZE (L2_ENTRY_NUM * 4)

#define GET_L1_INDEX(addr) ((addr) >> 20)
#define GET_L2_INDEX(addr) (((addr) & 0xff000) >> 12)

#define GET_PAGE_SIZE(size) (((size) + PAGE_SIZE - 1) >> 12)

#define FL_PAGE_TABLE       0x1

#define SL_SHORT_DESCRIPTOR 0x2
#define SL_B                0x4
#define SL_C                0x8
#define SL_S                0x400

#define AP_PRIVILEGED_ACCESS 0x10
#define AP_FULL_ACCESS       0x30

#define DA_NO_ACCESS 0x0
#define DA_CLIENT    0x1
#define DA_MANAGER   0x3

#define VIRT_VECTORS_ADDR 0xffff0000

extern char vectors_start;
extern char vectors_end;

struct mapping {
  struct list next;
  pid_t pid;
  uint32_t *address;
};

static struct list mappings;
static struct slab_cache *mapping_cache;

static uint32_t *mmu_create_pl1(void) {
  return memset(page_address(buddy_alloc(L1_SIZE)), 0, L1_SIZE);
}

static uint32_t *mmu_create_pl2(void) {
  return memset(page_address(buddy_alloc(L2_SIZE)), 0, L2_SIZE);
}

static uint32_t *mmu_create_page(void) {
  return page_address(buddy_alloc(PAGE_SIZE));
}

static uint32_t *mmu_create_and_fill_pl2(uint32_t *pl1, uint32_t l1_i) {
  uint32_t i, *pl2;
  (void)i;

  if (!(pl1[l1_i] & FL_PAGE_TABLE)) {
    pl2 = mmu_create_pl2();
    pl1[l1_i] = (0xfffffc00 & (uint32_t)pl2) | FL_PAGE_TABLE;
  }

  return (uint32_t*)(0xfffffc00 & pl1[l1_i]);
}

static int mmu_create_mapping(uint32_t *mapping, uint32_t addr, size_t size, bool is_privileged) {
  uint32_t i, *page, *pl2;
  uint32_t l1_i = GET_L1_INDEX(addr), l2_i = GET_L2_INDEX(addr);

  if (L2_ENTRY_NUM < (l2_i + GET_PAGE_SIZE(size))) {
    return -1;
  }

  pl2 = mmu_create_and_fill_pl2(mapping, l1_i);
  for (i = l2_i; i < (l2_i + GET_PAGE_SIZE(size)); ++i) {
    page = mmu_create_page();
    pl2[i] = (uint32_t)page | SL_SHORT_DESCRIPTOR | (is_privileged ? AP_PRIVILEGED_ACCESS : AP_FULL_ACCESS);
  }

  return 0;
}

static int mmu_create_straight_mapping(uint32_t *mapping, uint32_t addr, size_t size) {
  uint32_t i, new_addr, *pl2;
  uint32_t l1_i = GET_L1_INDEX(addr), l2_i = GET_L2_INDEX(addr);

  if (L2_ENTRY_NUM < (l2_i + GET_PAGE_SIZE(size))) {
    return -1;
  }

  pl2 = mmu_create_and_fill_pl2(mapping, l1_i);
  for (i = 0; i < GET_PAGE_SIZE(size); ++i) {
    new_addr = (addr & 0xfffff000) + (PAGE_SIZE * i);
    pl2[l2_i + i] = new_addr | SL_SHORT_DESCRIPTOR | AP_PRIVILEGED_ACCESS;
  }

  return 0;
}

static void mmu_create_vectors_mapping(uint32_t *mapping) {
  uint32_t l1_i = GET_L1_INDEX(VIRT_VECTORS_ADDR);
  uint32_t l2_i = GET_L2_INDEX(VIRT_VECTORS_ADDR);

  uint32_t *pl2 = mmu_create_and_fill_pl2(mapping, l1_i);
  uint32_t *page = mmu_create_page();

  memcpy(page, &vectors_start, (&vectors_end - &vectors_start));
  pl2[l2_i] = (uint32_t)page | SL_SHORT_DESCRIPTOR | AP_PRIVILEGED_ACCESS;
}

static int mmu_create_kernel_mappings(uint32_t *mapping) {
  int i;

  /* Exception Vectors */
  mmu_create_vectors_mapping(mapping);

  /* Kernel [0x60000000 - 0x69000000] */
  for (i = 0; i < 0x90; ++i) {
    mmu_create_straight_mapping(mapping, 0x60000000 + (0x100000 * i), 0x100000);
  }

  /* Motherboard peripherals [0x10000000 - 0x10020000] */
  mmu_create_straight_mapping(mapping, 0x10000000, 0x20000);

  /* SCU [0x1e000000 - 0x1e002000] */
  mmu_create_straight_mapping(mapping, 0x1e000000, 0x2000);

  return 0;
}

static struct mapping *mmu_mapping_fetch(pid_t pid) {
  struct mapping *mapping;

  list_foreach(mapping, &mappings, next) {
    if (mapping->pid == pid) {
      return mapping;
    }
  }

  mapping = slab_cache_alloc(mapping_cache);
  mapping->pid = pid;

  mapping->address = mmu_create_pl1();
  mmu_create_kernel_mappings(mapping->address);

  list_add(&mappings, &mapping->next);
  return mapping;
}

void mmu_init(void) {
  list_init(&mappings);
  mapping_cache = slab_cache_create("mapping", sizeof(struct mapping));

  /* DACR */
  __asm__ (
    "MCR   p15, 0, %[domain], c3, c0, 0 \n\t"
    :
    : [domain] "r"(DA_CLIENT)
  );

  mmu_set_ttb(0);
}

int mmu_destroy(pid_t pid) {
  struct mapping *mapping = NULL;

  list_foreach(mapping, &mappings, next) {
    if (mapping->pid == pid) {
      break;
    }
  }

  if (!mapping) {
    return -1;
  }

  mmu_set_ttb(0);
  // TODO: release allocated memory

  list_remove(&mapping->next);
  slab_cache_free(mapping_cache, mapping);

  return 0;
}

pid_t mmu_set_ttb(pid_t pid) {
  static pid_t current_pid = 0;

  pid_t old_pid;
  struct mapping *mapping = mmu_mapping_fetch(pid);
  uint32_t addr = (uint32_t)mapping->address;

  mmu_disable();

  /* TTBR0 */
  __asm__ (
    "MCR   p15, 0, %[ttb], c2, c0, 0 \n\t"
    :
    : [ttb] "r"(addr & 0xffffc000)
  );

  mmu_enable();

  old_pid = current_pid;
  current_pid = pid;

  return old_pid;
}

int mmu_alloc(pid_t pid, uint32_t addr, size_t size) {
  struct mapping *mapping = mmu_mapping_fetch(pid);
  return mmu_create_mapping(mapping->address, addr, size, false);
}
