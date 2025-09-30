/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    /**
    * TODO: implement per description
    */

    uint8_t current_pos = buffer->out_offs;
    size_t total_bytes = 0;
    uint8_t entries_checked = 0;

    // Iterate through the entries in the buffer 
    while(entries_checked < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {

        //retrieve entry at current position
        struct aesd_buffer_entry *entry = &buffer->entry[current_pos];

        // Check to see if we are at our offset target
        if(char_offset < total_bytes + entry->size){
            // we have found our entry we need to return, first set byte_rtn
            *entry_offset_byte_rtn = char_offset = char_offset - total_bytes;
            return entry;
        }

        // Add entry byte size to our total we are tracking
        total_bytes += entry->size;

        // Move to the next entry (if at end, wrap around)
        current_pos = (current_pos + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        entries_checked++;

        // Discontinue if we have went through all active entries and we have already reached in_offs and the buffer isn't full
        if(!buffer->full && current_pos == buffer->in_offs){
            break;
        }
    }


    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */

    // Store the new entry in the buffer, always place the new entry in the in_offs position
    buffer->entry[buffer->in_offs] = *add_entry;

    // If the buffer was already full, we need to move ahead out_offs and overwrite the old entry
    if(buffer->full){
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // Advance the in_oofs since we are writing and moving to the next position in the buffer
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // Set flag if buffer is full or false if not full
    buffer->full = (buffer->in_offs == buffer->out_offs);
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
