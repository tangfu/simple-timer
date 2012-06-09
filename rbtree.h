#ifndef __RBTREE_H__
#define __RBTREE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>



typedef int key_t;

typedef void* data_t;

typedef enum color_t {RED = 0, BLACK = 1 } color_t;

typedef struct rb_node_t {
        struct rb_node_t *left, *right, *parent;
        key_t key;
        data_t data;
        color_t color;
} rb_node_t;


/* forward declaration */

rb_node_t *rb_insert( key_t key, data_t data, rb_node_t * root );
rb_node_t *rb_search( key_t key, rb_node_t * root );
rb_node_t *rb_erase( key_t key, rb_node_t * root );

#endif	/* __RBTREE_H__  */