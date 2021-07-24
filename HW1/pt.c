#include <stdlib.h>
#include <stdio.h>
#include "os.h"

/* - Theoretical Explanation :
 * Given that VPN is 20b-long, each node in the PT sized 4KB, each PTE is 32b long.
 * One must conclude that "d" in the multi-level PT must be 2^10, in order the satisfy the above.
 * Hence, the PT has two levels (root-level includes)
 *
 * - Implementation Explanation:
 * Implemented a function named 'tree_walk' that given VPN it walks on the tree
 * as "far" as possible following the "directions" from the VPN.
 * Both 'page_table_update' and 'page_table_query' utilizes its to retrieve
 * the desired node pointer from the PT.
 * */

/* =====================  Declarations ===================== */
/* ========  Bitwise arithmetic ======== */
/* gets the validity bit of a given PTE */
uint32_t valid_bit(uint32_t pte);
/* "breaks" given vpn: returns the 10-MSBs (index=0) or 10-LSBs (index=1) */
uint32_t break_vpn(uint32_t vpn, uint32_t index);

/* ========  Tree Walk ======== */
/* struct to represent the result returned from a tree_walk */
typedef struct walk_result_st
{
    /* pointer in the page table */
    uint32_t * pte_ptr;
    /* maximum level reached on the walk */
    int level_reached;
} walk_res_t;
/* walks on the multi-level-PT rooted in 'pt_root_ppn', returns the result of the walk in 'res' */
void tree_walk(uint32_t pt_root_ppn, uint32_t vpn, walk_res_t * res);


/* =====================  Implementation ===================== */
uint32_t valid_bit(uint32_t pte)
{
    return pte & 1;
}


uint32_t break_vpn(uint32_t vpn, uint32_t index)
{
    return (index == 1) ? (vpn & 0x3ff) : (vpn >> 10);
}


void tree_walk(uint32_t pt_root_ppn, uint32_t vpn, walk_res_t * res)
{
    /* breaks the vpn to the navigation direction in the multi-level-PT */
    uint32_t level_0_index = break_vpn(vpn,0);
    uint32_t level_1_index = break_vpn(vpn,1);

    uint32_t * level_0_node = (uint32_t *) phys_to_virt(pt_root_ppn << 12);
    uint32_t level_1_pte = level_0_node[level_0_index];
    if (valid_bit(level_1_pte) == 0)
    {
        /* no valid pointer to level 1. walk stops in level 0 */
        res->pte_ptr = level_0_node + level_0_index;
        res->level_reached = 0;
        return;
    }
    uint32_t * level_1_node = (uint32_t *) phys_to_virt(level_1_pte - 1);
    res->pte_ptr = level_1_node + level_1_index;
    res->level_reached = 1;
}


void page_table_update(uint32_t pt, uint32_t vpn, uint32_t ppn)
{
    walk_res_t res;
    tree_walk(pt, vpn, &res);
    uint32_t * pte_ptr = res.pte_ptr;
    /* DESTROY CASE */
    if (ppn == NO_MAPPING)
    {
        /* reached the desired mapping and it should be destroyed */
        if (res.level_reached == 1)
        {
            *pte_ptr = 0;
        }
        return;
    }
    /* CREATE CASE */
    if (res.level_reached == 0)
    {
        /* the VPN path doesnt exist fully, so allocates new node to level 1 */
        uint32_t new_level1_ppn = alloc_page_frame();
        *pte_ptr = (new_level1_ppn << 12) + 1;
        pte_ptr = (uint32_t *) phys_to_virt(new_level1_ppn << 12) + break_vpn(vpn,1);
    }
    /* updates current PTE */
    *pte_ptr = (ppn << 12) + 1;;
}


uint32_t page_table_query(uint32_t pt, uint32_t vpn)
{
    walk_res_t res;
    tree_walk(pt, vpn, &res);
    uint32_t * pte_ptr = res.pte_ptr;
    /* if walk ended in a dead-end, then PTE is not mapped */
    if (valid_bit(*pte_ptr) == 0)
    {
        return NO_MAPPING;
    }
    /* else, current PTE has valid ppn */
    return *pte_ptr >> 12;
}
