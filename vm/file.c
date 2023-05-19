/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
struct lazy_load_arg
{
	struct file *file;
	off_t ofs;
	uint32_t read_bytes;
	uint32_t zero_bytes;
};
static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
bool load_file_backed(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable);
void page_count_init(void* addr, uint32_t length);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	struct lazy_load_arg *arg = (struct lazy_load_arg *)page->uninit.aux;
	file_page->load_file = arg->file;
	file_page->size = arg->read_bytes;
	file_page->offset = arg->ofs;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct thread* current_thread = thread_current();
	if(pml4_is_dirty(current_thread->pml4, page->va)){
		
		file_write_at(file_page->load_file, page->va,file_page->size, file_page->offset);
		pml4_set_dirty(current_thread->pml4, page->va, false);
	}
	
	pml4_clear_page(current_thread->pml4, page->va);

}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	if(load_file_backed(file, offset, addr, length, 0, writable)){
		return addr;
	}
	return NULL;
}

bool
load_file_backed(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	void* addr = upage;
	uint32_t length = read_bytes;

	while (read_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct lazy_load_arg *aux = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
		aux->file = file;
		aux->ofs = ofs;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;
		if (!vm_alloc_page_with_initializer(VM_FILE, upage, writable, lazy_load_segment, aux)){
			return false;
		}	

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	page_count_init(addr, length);
	return true;
}

void
page_count_init(void* addr, uint32_t length){
	int page_count = length / PGSIZE;
	struct page* first_page;

	if(length % PGSIZE != 0){
		page_count = length / PGSIZE + 1;
	}

	first_page = spt_find_page(&thread_current()->spt, addr);
	first_page->page_count = page_count;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread* current_thread = thread_current();
	struct page* first_page = spt_find_page(&current_thread->spt, addr);
	// printf("do munmap\n");
	int page_count = first_page->page_count;
	for(int i = 0 ;i < page_count; i++){
		struct page* page = spt_find_page(&current_thread->spt, addr + i * PGSIZE);
		spt_remove_page(&current_thread->spt, page);
	}
}
