/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	list_init(&swap_table);
	disk_sector_t sector_number = disk_size(swap_disk); //size for swaptable
	int slot_number = sector_number / 8;
	for (int i = 0 ; i < slot_number ; i++){
		struct slot *uninit_slot = malloc(sizeof(struct slot));
		uninit_slot->slot_number = i;
		uninit_slot->page = NULL;
		list_push_back(&swap_table, &uninit_slot->swap_elem);
	}
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	/*
	insert new frame
	*/

}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	struct slot *swap_slot= find_swap_slot();
	swap_slot->page = page;
	disk_sector_t sector_number = swap_slot->slot_number * 8;
	for (int i = sector_number - 8 ; i < sector_number ; i++){
		disk_write(swap_disk, i, page->frame->kva);
	}
	palloc_free_page(page->frame->kva);
	pml4_clear_page(thread_current()->pml4, page->va);
}

struct slot*
find_swap_slot(){
	struct list_elem *slot_elem = list_begin(&swap_table);
	while(true){
		struct slot *swap_slot = list_entry(slot_elem, struct slot, swap_elem);
		if (swap_slot == list_end(&swap_table)){
			PANIC("OVER CAPACITY LIMIT");
		}
		if (swap_slot->page == NULL){
			return swap_slot;
		}
	}
} 
/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	pml4_clear_page(thread_current()->pml4, page->va);
}
