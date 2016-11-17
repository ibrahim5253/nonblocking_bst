#include <iostream>
#include <atomic>

/* States of change, a node can be in */

#define NONE     0   /* No change going on */
#define MARK     1   /* Node has been logically deleted */
#define CHILDCAS 2   /* One of the child pointers being modified */
#define RELOCATE 3   /* Node affected by relocation */

/* Macros to modify state of a node */

#define FLAG(ptr, state) ((((ptr)>>2)<<2)|(state))   /* Set the passed flag */
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
		//cout << "ABORT Alert\n";
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
		//cout << "Check 1" << endl;
		pred = curr;
		predOp = currOp;
		curr = next;
		currOp = reinterpret_cast<Node*>(curr)->op;
		if (GETFLAG(currOp) != NONE) {
			//cout << "Never come here\n";
			help(pred, predOp, curr, currOp);
			goto retry;
		}
		currKey = reinterpret_cast<Node*>(curr)->key;
		//cout << currKey << endl;
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
		//cout << "Node being added to " << reinterpret_cast<Node*>(curr)->key << endl;
		if (result == FOUND) return false;
		newNode = reinterpret_cast<long>(new Node(k));
		bool isLeft = (result == NOTFOUND_L);
		//cout << "Left Node? " << isLeft << endl;
		long old = isLeft ? reinterpret_cast<Node*>(curr)->left : reinterpret_cast<Node*>(curr)->right;
		//cout << "old: " << old << endl;
		casOp = reinterpret_cast<long>(new ChildCASOp(isLeft, old, newNode));
		//cout << "casOp: " << casOp << endl;
		//cout << "newNode: " << newNode << endl;
		if (reinterpret_cast<Node*>(curr)->op.compare_exchange_strong(currOp, FLAG(casOp, CHILDCAS))) {
			helpChildCAS(casOp, curr);
			////cout << ISNULL(reinterpret_cast<Node*>(curr)->right) << endl;
			//cout << "Right Node address: " << reinterpret_cast<Node*>(curr)->right << endl;
			return true;
		}
	}
}

void NonBlockingBST :: helpChildCAS(long op, long dest) {
	//cout << "op: " << op << endl;
	Node* dest_p     = reinterpret_cast<Node*>(dest);
	ChildCASOp *op_p = reinterpret_cast<ChildCASOp*>(op);
	
	//cout << "Status before CAS in helpChildCAS:\n";
	//cout << "Is left? " << op_p -> isLeft << endl;
	//cout << "dest_p->right : " << dest_p->right << endl;
	//cout << "op_p->expected : " << op_p->expected << endl;
	//cout << "op_p->update : " << op_p->update << endl;
	(op_p->isLeft?dest_p->left:dest_p->right).compare_exchange_strong(op_p->expected, op_p->update);
	//cout << "Dest right: " << dest_p->right << endl;
	long tmp = FLAG(op, CHILDCAS);
	dest_p->op.compare_exchange_strong(tmp, FLAG(op, NONE));
}

void NonBlockingBST :: help(long pred, long predOp, long curr, long currOp)
{
}

int main()
{
	NonBlockingBST B;
	while(1) {
		cout << "1:I, 2:S, 3:D, 4:E\n";
		int ch, val;cin>>ch>>val;
		cout << "Status: ";
		switch(ch) {
			case 1:
				cout << B.add(val);
				break;
			case 2:
				cout << B.contains(val);
				break;
			case 3:
			case 4:
			default:
				return 0;
		}
		cout << endl;
	}
	return 0;
}
