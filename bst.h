#ifndef _BST_H_
#define _BST_H_

#include <iostream>
#include <atomic>

/* States of change, a node can be in */

#define NONE     0   /* No change going on */
#define MARK     1   /* Node has been logically deleted */
#define CHILDCAS 2   /* One of the child pointers being modified */
#define RELOCATE 3   /* Node affected by relocation */

/* Macros to modify state of a node */

#define FLAG(ptr, state) (reinterpret_cast<Operation*>(((reinterpret_cast<long>((ptr))>>2)<<2)|(state)))   /* Set the passed flag */
#define GETFLAG(ptr)     (reinterpret_cast<long>((ptr)) & 3)                                               /* Get current status */
#define UNFLAG(ptr)      (reinterpret_cast<Operation*>(reinterpret_cast<long>((ptr)) & ~0<<2))             /* Clear all the flags  */

/* Manipulate node pointers */

#define SETNULL(ptr)      (reinterpret_cast<Node*>(reinterpret_cast<long>((ptr)) |  1))       /* Set the null bit */
#define ISNULL(ptr)       (reinterpret_cast<long>((ptr)) &  1)                                /* Check if null */

/* Possible states of a relocation operation */
enum relocation {ONGOING, SUCCESSFUL, FAILED};

using namespace std;

class Operation {};

class Node { 
	public:
		atomic<int> key;
		atomic<Operation*> op;
		atomic<Node*> left;
		atomic<Node*> right;

		Node(int k) : 
			key(k), op(0), left(reinterpret_cast<Node*>(1)), 
			right(reinterpret_cast<Node*>(1)) {}
	         
};

class ChildCASOp : public Operation {
	public:
		bool isLeft;
		Node* expected;
		Node* update;
	
		ChildCASOp(bool f, Node* old, Node* newNode) :
			isLeft(f), expected(old), update(newNode) {}
};

class RelocateOp : public Operation {
	public:
		atomic<int> state;
		Node* dest;
		Operation* destOp;
		int removeKey;
		int replaceKey;

		RelocateOp (Node* d, Operation* dop, int rm_key, int rep_key) :
			state(ONGOING), dest(d), destOp(dop), removeKey(rm_key), replaceKey(rep_key) {}
};

class NonBlockingBST {
	public:
		Node root;  /* Not actual root, just a dummy */

		/* Possible outcomes of a search operation */
		enum result {FOUND, NOTFOUND_L, NOTFOUND_R, ABORT};


		NonBlockingBST() : root(-1) {}
		bool contains (int k);
		int find(int k, Node*& pred, Operation*& predOp, Node*& curr,
				Operation*& currOp, Node* auxRoot);
		bool add(int k);
		void helpChildCAS(Operation* op, Node* dest);
		bool remove(int k);
		bool helpRelocate(Operation* op, Node* pred, Operation* predOp, Node* curr);
		void helpMarked(Node* pred, Operation* predOp, Node* curr);
		void help(Node* pred, Operation* predOp, Node* curr, Operation* currOp);
};

#endif  // _BST_H_

