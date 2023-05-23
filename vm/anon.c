/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);
struct slot* find_swap_slot(struct page *swap_page);

enum swap{
	SECOTR_PER_SLOT = PGSIZE/DISK_SECTOR_SIZE,
};
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
	lock_init(&swap_table_lock);
	lock_init(&lazy_load_lock);
	disk_sector_t sector_number = disk_size(swap_disk); //size for swaptable
	int slot_number = (sector_number) / SECOTR_PER_SLOT;
	for (int i = 1 ; i <= slot_number ; i++){
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
	struct slot *swap_slot= find_swap_slot(page);
	disk_sector_t sector_number = swap_slot->slot_number * SECOTR_PER_SLOT;
	int offset = 0;
	lock_acquire(&swap_table_lock);
	for (int i = sector_number - SECOTR_PER_SLOT ; i < sector_number ; i++){
		disk_read(swap_disk, i, (char*)kva + (DISK_SECTOR_SIZE * offset));
		offset ++;
	}
	lock_release(&swap_table_lock);
	swap_slot->page = NULL;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct slot *swap_slot= find_swap_slot(NULL);
	swap_slot->page = page;
	disk_sector_t sector_number = swap_slot->slot_number * SECOTR_PER_SLOT;

	int offset = 0;
	
	lock_acquire(&swap_table_lock);
	for (int i = sector_number - SECOTR_PER_SLOT ; i < sector_number ; i++){
		disk_write(swap_disk, i, (char*)(page->frame->kva) + (DISK_SECTOR_SIZE * offset));
		offset ++;
	}
	lock_release(&swap_table_lock);
	pml4_clear_page(thread_current()->pml4, page->va); // 수정필요
}

struct slot*
find_swap_slot(struct page *swap_page){
	lock_acquire(&swap_table_lock);
	struct list_elem *slot_elem = list_begin(&swap_table);
	while(true){
		struct slot *swap_slot = list_entry(slot_elem, struct slot, swap_elem);
		if (swap_slot == list_end(&swap_table)){
			PANIC("OVER CAPACITY LIMIT");
		}
		if (swap_slot->page == swap_page){
			lock_release(&swap_table_lock);
			return swap_slot;
		}
		slot_elem = list_next(slot_elem);
	}
} 
/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	/*frame table 삭제*/
	lock_acquire(&frame_table_lock);
	list_remove (&page->frame->frame_elem);
	// free(page->frame);
	palloc_free_page(page->frame->kva);
	pml4_clear_page(thread_current()->pml4, page->va);
	lock_release(&frame_table_lock);
}
