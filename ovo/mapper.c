#include "mapper.h"
#include "debug.h"

static uintptr_t get_kernel_pgd(void)
{
	uintptr_t ttbr1;
	uintptr_t pgd_phys;
	uintptr_t pgd_virt;
	// 读取TTBR1_EL1寄存器
	asm volatile("mrs %0, ttbr1_el1" : "=r"(ttbr1));
	pgd_phys = ttbr1 & 0xfffffffff000;
	pgd_virt = (unsigned long)phys_to_virt(pgd_phys);
	return pgd_virt;
}

pte_t *get_kernel_ptep(uintptr_t va)
{
	pgd_t *pgd_base = (pgd_t *)get_kernel_pgd();
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;
	RWPM_DEBUG("RWPM: get_kernel_ptep");
	RWPM_DEBUG("RWPM: va: %px", (void *)va);
	if (!pgd_base)
	{
		RWPM_DEBUG("RWPM: pgd_base is NULL");
		return NULL;
	}
	RWPM_DEBUG("RWPM: pgd_base: %px", (void *)pgd_base);
	pgd = pgd_base + pgd_index(va);
	RWPM_DEBUG("RWPM: pgd: %px", (void *)pgd);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
	{
		return NULL;
	}
	p4d = p4d_offset(pgd, va);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
	{
		return NULL;
	}
	pud = pud_offset(p4d, va);
	if (pud_none(*pud) || pud_bad(*pud))
	{
		return NULL;
	}
	pmd = pmd_offset(pud, va);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
	{
		return NULL;
	}
	ptep = pte_offset_kernel(pmd, va);
	if (!ptep)
	{
		return NULL;
	}
	RWPM_DEBUG("RWPM: ptep: %px", (void *)ptep);
	return ptep;
}

uint8_t mapper_page[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static pte_t *mapper_ptep = NULL;
static pte_t orig_pte;

int init_mapper(void)
{
	RWPM_DEBUG("RWPM: init_mapper");
	mapper_ptep = get_kernel_ptep((uintptr_t)mapper_page);
	if (!mapper_ptep)
	{
		RWPM_ERROR("RWPM: get_kernel_ptep failed");
		return -1;
	}
	RWPM_DEBUG("RWPM: mapper_ptep: %px", (void *)mapper_ptep);
	orig_pte = READ_ONCE(*mapper_ptep);
	return 0;
}

void map_phys_page(uintptr_t pa)
{
	pte_t new_pte;
	RWPM_DEBUG("RWPM: map_phys_page");
	RWPM_DEBUG("RWPM: pa: %px", (void *)pa);
	new_pte = pfn_pte(__phys_to_pfn(pa), pgprot_noncached(PAGE_KERNEL));
	WRITE_ONCE(*mapper_ptep, new_pte);
	flush_tlb_kernel_range((uintptr_t)mapper_page,
						   (uintptr_t)mapper_page + PAGE_SIZE);
}

void destroy_mapper(void)
{
	RWPM_DEBUG("RWPM: destroy_mapper");
	WRITE_ONCE(*mapper_ptep, orig_pte);
	flush_tlb_kernel_range((uintptr_t)mapper_page,
						   (uintptr_t)mapper_page + PAGE_SIZE);
}