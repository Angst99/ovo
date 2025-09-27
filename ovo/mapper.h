#pragma once

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

extern uint8_t mapper_page[PAGE_SIZE];

pte_t *get_kernel_ptep(uintptr_t va);
int init_mapper(void);
void map_phys_page(uintptr_t pa);
void destroy_mapper(void);