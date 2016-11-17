#include <iostream>
#include <atomic>

/* States of change, a node can be in */

#define NONE     0   /* No change going on */
#define MARK     1   /* Node has been logically deleted */
#define CHILDCAS 2   /* One of the child pointers being modified */
#define RELOCATE 3   /* Node affected by relocation */

/* Macros to modify state of a node */

#define FLAG(ptr, state) ((ptr) |= (state))  /* Set the passed flag */
#define GETFLAG(ptr)     ((ptr) & 3)         /* Get current status */
#define UNFLAG(ptr)      ((ptr) &= ~0<<2)    /* Clear all the flags  */

/* Manipulate node pointers */

#define SETNULL(ptr)      ((ptr) |= 1)       /* Set the null bit */
#define ISNULL(ptr)       ((ptr) &  1)       /* Check if null */

using namespace std;

class Operation {};

class Node { 
	public:
		atomic<int> key;
		atomic<long> op;
		atomic<long> left;
		atomic<long> right;

		Node(int k) : 
			key(k), op(0), left(1), right(1) {}
	         
};

class ChildCASOp : public Operation {
	public:
		bool isLeft;
		long expected;
		long update;
	
		ChildCASOp(bool f, long old, long newNode) :
			isLeft(f), expected(old), update(newNode) {}
};

class RelocateOp : public Operation {
	public:
		atomic<int> state;
		long dest;
		long destOp;
		int removeKey;
		int replaceKey;
};

class NonBlockingBST {
	public:
		Node root;  /* Not actual root, just a dummy */

		/* Possible outcomes of a search operation */
		enum result {FOUND, NOTFOUND_L, NOTFOUND_R, ABORT};

		NonBlockingBST() : root(-1) {}
		bool contains (int k);
		int find(int k, long& pred, long& predOp, long& curr,
				long& currOp, long auxRoot);
		bool add(int k);
		void helpChildCAS(long op, long dest);
		bool remove(int k);
		bool helpRelocate(long op, long pred, long predOp, long curr);
		void helpMarked(long pred, long predOp, long curr);
		void help(long pred, long predOp, long curr, long currOp);
};

bool NonBlockingBST :: contains(int k)
{
	long pred, curr;
	long predOp, currOp;
	return find(k, pred, predOp, curr, currOp, reinterpret_cast<long>(&root)) == FOUND;
}

int NonBlockingBST :: find(int k, long& pred, long& predOp,
	                   long& curr, long& currOp, long auxRoot)
{
	int result, currKey;
	long next, lastRight;
	long lastRightOp;

    retry:
	result = NOTFOUND_R;
	curr = auxRoot;
	currOp = reinterpret_cast<Node*>(curr)->op;
	if (GETFLAG(currOp) != NONE) {
		if (auxRoot == reinterpret_cast<long>(&root)) {
			helpChildCAS(UNFLAG(currOp), curr);
			goto retry;
		} 
		else return ABORT;
	}
	next = reinterpret_cast<Node*>(curr)->right;
	lastRight = curr;
	lastRightOp = currOp;
	while (!ISNULL(next)) {
		pred = curr;
		predOp = currOp;
		curr = next;
		currOp = reinterpret_cast<Node*>(curr)->op;
		if (GETFLAG(currOp) != NONE) {
			help(pred, predOp, curr, currOp);
			goto retry;
		}
		currKey = reinterpret_cast<Node*>(curr)->key;
		if (k < currKey) {
			result = NOTFOUND_L;
			next = reinterpret_cast<Node*>(curr)->left;
		}
		else if (k > currKey) {	
			result = NOTFOUND_R;
			next   = reinterpret_cast<Node*>(curr)->right;
			lastRight = curr;
			lastRightOp = currOp;
		}
		else {
			result = FOUND;
			break;
		}
	}
	if ((result != FOUND) && (lastRightOp != reinterpret_cast<Node*>(lastRight)->op))
		goto retry;
	if (reinterpret_cast<Node*>(curr)->op != currOp) goto retry;
	return result;
}	

bool NonBlockingBST :: add (int k) 
{
	long pred, curr, newNode;
	long predOp, currOp, casOp;
	int result;
	while(true) {
		result = find(k, pred, predOp, curr, currOp, reinterpret_cast<long>(&root));
		if (result == FOUND) return false;
		newNode = reinterpret_cast<long>(new Node(k));
		bool isLeft = (result == NOTFOUND_L);
		long old = isLeft ? reinterpret_cast<Node*>(curr)->left : reinterpret_cast<Node*>(curr)->right;
		casOp = reinterpret_cast<long>(new ChildCASOp(isLeft, old, newNode));
		if (reinterpret_cast<Node*>(curr)->op.compare_exchange_strong(currOp, FLAG(casOp, CHILDCAS))) {
			helpChildCAS(casOp, curr);
			return true;
		}
	}
}

void NonBlockingBST :: helpChildCAS(long op, long dest) {
	Node* dest_p     = reinterpret_cast<Node*>(dest);
	ChildCASOp *op_p = reinterpret_cast<ChildCASOp*>(op);
	
	(op_p->isLeft?dest_p->left:dest_p->right).compare_exchange_strong(op_p->expected, op_p->update);
}

int main()
{
	return 0;
}
