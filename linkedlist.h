#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

typedef struct node {
   int value;
   struct node *next;
} node;

node * initLinkedList() {
   node *dummy_head = malloc(sizeof(node));
   if(dummy_head == NULL) {
      perror("linkedlist: malloc error");
      exit(1);
   }
   dummy_head->next = NULL;
   return dummy_head;
}

void insertNode(int x, node *dummy_head) {
   node *curr;
   curr = malloc(sizeof(node));
   if(curr == NULL) {
      perror("linkedlist: malloc error");
      exit(1);
   }
   curr->value = x;
   curr->next = dummy_head->next;
   dummy_head->next = curr;
}

void deleteNode(int del, node *dummy_head) {
   node *curr = dummy_head;
   while(curr->next != NULL) {
      if(curr->next->value == del) {
         node *temp = curr->next;
         curr->next = curr->next->next;
         free(temp);
         break;
      }
      curr = curr->next;
   }
}

/* check whether a linkedlist contains an element or not. Return 1 if contains, return 0 if not */
int containsNode(int target, node *dummy_head) {
   node *curr = dummy_head;
   while(curr->next != NULL) {
      if(curr->next->value == target) {
         return 1;
      }
      curr = curr->next;
   }
   return 0;
}

