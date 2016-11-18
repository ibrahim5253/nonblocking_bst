#include <iostream>
#include <atomic>

/* States of change, a node can be in */

#define NONE     0   /* No change going on */
#define MARK     1   /* Node has been logically deleted */
#define CHILDCAS 2   /* One of the child pointers being modified */
#define RELOCATE 3   /* Node affected by relocation */

/* Macros to modify state of a node */

#define FLAG(ptr, state) ((((ptr)>>2)<<2)|(state))   /* Set the passed flag */
#define GETFLAG(ptr)     ((ptr) & 3)                 /* Get current status */
#define UNFLAG(ptr)      ((ptr) & ~0<<2)            /* Clear all the flags  */

/* Manipulate node pointers */

#define SETNULL(ptr)      ((ptr) |  1)       /* Set the null bit */
#define ISNULL(ptr)       ((ptr) &  1)       /* Check if null */

/* Possible states of a relocation operation */
enum relocation {ONGOING, SUCCESSFUL, FAILED};

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

		RelocateOp (long d, long dop, int rm_key, int rep_key) :
			state(ONGOING), dest(d), destOp(dop), removeKey(rm_key), replaceKey(rep_key) {}
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
	long tmp = FLAG(op, CHILDCAS);
	dest_p->op.compare_exchange_strong(tmp, FLAG(op, NONE));
}

bool NonBlockingBST :: remove (int k)
{
	long pred, curr, replace;
	long predOp, currOp, replaceOp, relocOp;
	while (true) {
		if (find(k,pred,predOp,curr,currOp,reinterpret_cast<long>(&root))!=FOUND) return false;
		Node* curr_p = reinterpret_cast<Node*>(curr);
		if (ISNULL(curr_p->right) || ISNULL(curr_p->left)) {
			// Node has < 2 children
			if (curr_p->op.compare_exchange_strong(currOp, FLAG(currOp, MARK))) {
				helpMarked(pred, predOp, curr);
				return true;
			}
		}
		else {
			// Node has 2 children
			if (find(k, pred, predOp, replace, replaceOp, curr)==ABORT || curr_p->op!=currOp) continue;
			relocOp = reinterpret_cast<long>(new RelocateOp(curr, currOp, k, reinterpret_cast<Node*>(replace)->key));
			if (reinterpret_cast<Node*>(replace)->op.compare_exchange_strong(replaceOp,FLAG(relocOp, RELOCATE))) {
				if (helpRelocate(relocOp, pred, predOp, replace)) return true;
			}
		}
	}
}

bool NonBlockingBST ::  helpRelocate(long op, long pred, long predOp, long curr)
{
	RelocateOp* op_p = reinterpret_cast<RelocateOp*>(op);
	Node* op_dest = reinterpret_cast<Node*>(op_p->dest);
	int seenState = op_p->state;
	if (seenState == ONGOING) {
		long seenOp = __sync_val_compare_and_swap(reinterpret_cast<long*>(&op_dest->op), op_p->destOp, FLAG(op, RELOCATE));
		if ((seenOp==op_p->destOp) || (seenOp==FLAG(op, RELOCATE))) {
			int exp_state = ONGOING;
			op_p->state.compare_exchange_strong(exp_state, SUCCESSFUL);
			seenState = SUCCESSFUL;
		}
		else {
			seenState = __sync_val_compare_and_swap(reinterpret_cast<int*>(&op_p->state), ONGOING, FAILED);
		}
	}
	if (seenState == SUCCESSFUL) {
		op_dest->key.compare_exchange_strong(op_p->removeKey, op_p->replaceKey);
		long tmp = FLAG(op, RELOCATE);
		op_dest->op.compare_exchange_strong(tmp, FLAG(op, NONE));
	}
	bool result = (seenState == SUCCESSFUL);
	if (op_p->dest == curr) return result;
	long tmp = FLAG(op, RELOCATE);
	reinterpret_cast<Node*>(curr)->op.compare_exchange_strong(tmp, FLAG(op, result?MARK:NONE));
	if (result) {
		if (op_p->dest == pred) predOp = FLAG(op, NONE);
		helpMarked(pred, predOp, curr);
	}
	return result;
}

void NonBlockingBST :: helpMarked(long pred, long predOp, long curr)
{
	long newRef;
	Node* curr_p = reinterpret_cast<Node*>(curr);
	Node* pred_p = reinterpret_cast<Node*>(pred);
	if (ISNULL(curr_p->left)) {
		if (ISNULL(curr_p->right))
			newRef = SETNULL(curr);
		else
			newRef = curr_p->right;
	}
	else    newRef = curr_p->left;
	long casOp = reinterpret_cast<long>(new ChildCASOp(curr == pred_p->left, curr, newRef));
	if (pred_p->op.compare_exchange_strong(predOp, FLAG(casOp, CHILDCAS)))
		helpChildCAS(casOp, pred);
}

void NonBlockingBST :: help(long pred, long predOp, long curr, long currOp)
{
	if (GETFLAG(currOp)==CHILDCAS)
		helpChildCAS(UNFLAG(currOp), curr);
	else if (GETFLAG(currOp)==RELOCATE)
		helpRelocate(UNFLAG(currOp), pred, predOp, curr);
	else if (GETFLAG(currOp)==MARK)
		helpMarked(pred, predOp, curr);
}

int main()
{
	NonBlockingBST B;
	while(1) {
		int ch, val;
		cout << "1:I, 2:S, 3:D, 4:E\n";cin>>ch>>val;
		cout << "Status: ";
		switch(ch) {
			case 1:
				cout << B.add(val);
				break;
			case 2:
				cout << B.contains(val);
				break;
			case 3:
				cout << B.remove(val);
				break;
			case 4:
			default:
				return 0;
		}
		cout << endl;
	}
	return 0;
}
