#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <memory.h>

#define ARG_ERROR 1
#define FILE_ERROR 2
#define TLB_SIZE 16
#define PAGES 256
#define PAGE_MASK 0xff
#define PAGE_SIZE 256
#define OFFSET_BITS 8
#define OFFSET_MASK 0xff
#define MEMORY_SIZE PAGES * PAGE_SIZE
#define BUFFER_SIZE 80

// Pointer to the backing store, used by the paging system to store information that's not currently in main memory.
void *backing = NULL;

// ith entry contains the logical page's frame number. The ith entry may contain -1 if the frame number is not present.
int pagetable[PAGES];

char main_memory[MEMORY_SIZE];

// Allows for fast retrieval of physical frame numbers (Although limited in the number of physical frames it can hold).
typedef struct {
    unsigned char logical;
    unsigned char physical;
} tlbentry;
tlbentry tlb[TLB_SIZE];
int tlb_index = 0;

typedef enum hit_type {TLB_HIT, PAGE_HIT, BSTORE_HIT} hit_type;

// Contains various statistical information about the paging system
typedef struct stats stats;
struct stats{
    size_t total_addresses;
    size_t tlb_hits;
    size_t page_faults;
};

/**
 * Helper function for printing the paging system's stats
 *
 * @param st - a stats structure
 *
 */
void print_stats(stats *st){
    printf("Total addresses translated: %zu", st->total_addresses);
    printf("\nPage faults: %zu", st->page_faults);
    printf("\nPage fault rate: %0.3f", st->page_faults / (1.0 * st->total_addresses));
    printf("\nTLB hits: %zu", st->tlb_hits);
    printf("\nTLB hit rate: %0.3f", st->tlb_hits / (1.0 * st->total_addresses));
}


/**
 * Adds a new entry to tlb table, and increments the current index pointer.
 *
 * @param logical  - A logical page number.
 * @param physical - A physical frame number.
 *
 */
void add_to_tlb(unsigned char logical, unsigned char physical){
    tlbentry* entry = &tlb[tlb_index % TLB_SIZE];
    tlb_index++;
    entry->logical = logical;
    entry->physical = physical;
}

/**
 * Helper function to find max of two numbers
 *
 * @param a  - An integer
 * @param b - Another integer to compare to a
 *
 */
int max(int a, int b){
    if(a > b) return a;
    return b;
}

/**
 * Given a logical page, this function searches the TLB table for
 * the corresponding physical frame.
 *
 * @param logical_page - A logical page.
 *
 * @return The physical frame number, or -1 if not found.
 *
 */
int search_tlb(int logical_page){
    for(int i = max((tlb_index - TLB_SIZE), 0); i < tlb_index; i++){
        tlbentry *entry = &tlb[i % TLB_SIZE];
        if(entry->logical == logical_page) { return entry->physical; }
    }
    return -1;
}


/**
 * Translates logical addresses into physical addresses.
 *
 * @param input_fp - The file to read line by line and translate from.
 *
 */
void translate_logical_to_physical(FILE *input_fp){
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    memset(pagetable, -1, PAGES * sizeof(char));

    stats st = {.page_faults = 0, .tlb_hits = 0, .total_addresses = 0};
    unsigned char free_page = 0;

    while(fgets(buffer, BUFFER_SIZE, input_fp) != NULL){
        hit_type type;
        st.total_addresses++;
        char *ptr;
        int logical_address = strtol(buffer, &ptr, 10); // Convert logical address string to int
        printf("virtual address: 0x%4x", logical_address);
        int offset = logical_address & OFFSET_MASK;
        int logical_page = (logical_address >> OFFSET_BITS) & PAGE_MASK;
        printf("(pg:0x%3x,off:0x%3x---> ", logical_page, offset);

        int physical_page = search_tlb(logical_page);
        if(physical_page != -1){
            type = TLB_HIT;
            st.tlb_hits++;
        } else if((physical_page = pagetable[logical_page]) != -1){
            type = PAGE_HIT;
            add_to_tlb(logical_page, physical_page);
        } else{
            type = BSTORE_HIT;
            st.page_faults++;
            physical_page = free_page;
            free_page++;
            // Copy page from secondary storage into physical memory
            memcpy(main_memory + physical_page * PAGE_SIZE, (char*)backing + logical_page * PAGE_SIZE, PAGE_SIZE);
            pagetable[logical_page] = physical_page;
            add_to_tlb(logical_page, physical_page);
        }

        int physical_address = (physical_page << OFFSET_BITS) | offset;
        char value = main_memory[physical_page * PAGE_SIZE + offset];


        if(type == TLB_HIT){ printf("%-11s, -----------, -----------,  ", " *IN_TLB*  "); }
        else if(type == PAGE_HIT) { printf("-----------, %-11s, -----------,  ", "*IN_P_TBLE*"); }
        else { printf("-----------, -----------, %-11s,  ", "*IN_BACK_S*"); }

        printf("physical address: 0x%04x, val: %3d   tlb_Hits:  %zu\n", physical_address, value, st.tlb_hits);
        if(st.total_addresses % 5 == 0) { printf("\n"); }
    }
    // Display TLB hits and page fault stats
    print_stats(&st);
}

// Driver code
int main(int argc, const char **argv) {

    if(argc != 3){
        fprintf(stderr, "Usage: ./virtmem backingstore input\n");
        exit(ARG_ERROR);
    }

    // Load backing store into memory
    const char *backing_filename = argv[1];
    int backing_fd = open(backing_filename, O_RDONLY);
    backing = mmap(0, MEMORY_SIZE, PROT_READ, MAP_PRIVATE, backing_fd, 0);

    const char *input_filename = argv[2];
    FILE *input_fp = fopen(input_filename, "r");

    if(input_fp == NULL){
        fprintf(stderr, "Error opening input file");
        exit(FILE_ERROR);
    }

    translate_logical_to_physical(input_fp);

    return 0;
}