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

bool NonBlockingBST :: contains(int k)
{
	Node *pred, *curr;
	Operation *predOp, *currOp;
	return find(k, pred, predOp, curr, currOp, &root) == FOUND;
}

int NonBlockingBST :: find(int k, Node*& pred, Operation*& predOp,
	                   Node*& curr, Operation*& currOp, Node* auxRoot)
{
	int result, currKey;
	Node *next, *lastRight;
	Operation* lastRightOp;

    retry:
	result = NOTFOUND_R;
	curr = auxRoot;
	currOp = curr->op;
	if (GETFLAG(currOp) != NONE) {
		if (auxRoot == &root) {
			helpChildCAS(UNFLAG(currOp), curr);
			goto retry;
		} 
		else return ABORT;
	}
	next = curr->right;
	lastRight = curr;
	lastRightOp = currOp;
	while (!ISNULL(next)) {
		pred = curr;
		predOp = currOp;
		curr = next;
		currOp = curr->op;
		if (GETFLAG(currOp) != NONE) {
			help(pred, predOp, curr, currOp);
			goto retry;
		}
		currKey = curr->key;
		if (k < currKey) {
			result = NOTFOUND_L;
			next = curr->left;
		}
		else if (k > currKey) {	
			result = NOTFOUND_R;
			next   = curr->right;
			lastRight = curr;
			lastRightOp = currOp;
		}
		else {
			result = FOUND;
			break;
		}
	}
	if ((result != FOUND) && (lastRightOp != lastRight->op))
		goto retry;
	if (curr->op != currOp) goto retry;
	return result;
}	

bool NonBlockingBST :: add (int k) 
{
	Node *pred, *curr, *newNode;
	Operation *predOp, *currOp, *casOp;
	int result;
	while(true) {
		result = find(k, pred, predOp, curr, currOp, &root);
		if (result == FOUND) return false;
		newNode = new Node(k);
		bool isLeft = (result == NOTFOUND_L);
		Node* old = isLeft ? curr->left : curr->right;
		casOp = new ChildCASOp(isLeft, old, newNode);
		if (curr->op.compare_exchange_strong(currOp, FLAG(casOp, CHILDCAS))) {
			helpChildCAS(casOp, curr);
			return true;
		}
	}
}

void NonBlockingBST :: helpChildCAS(Operation* op, Node* dest) {
	ChildCASOp *op_p = reinterpret_cast<ChildCASOp*>(op);
	
	(op_p->isLeft?dest->left:dest->right).compare_exchange_strong(op_p->expected, op_p->update);
	Operation* tmp = FLAG(op_p, CHILDCAS);
	dest->op.compare_exchange_strong(tmp, FLAG(op_p, NONE));
}

bool NonBlockingBST :: remove (int k)
{
	Node *pred, *curr, *replace;
	Operation *predOp, *currOp, *replaceOp, *relocOp;
	while (true) {
		if (find(k,pred,predOp,curr,currOp,&root)!=FOUND) return false;
		if (ISNULL(curr->right.load()) || ISNULL(curr->left.load())) {
			// Node has < 2 children
			if (curr->op.compare_exchange_strong(currOp, FLAG(currOp, MARK))) {
				helpMarked(pred, predOp, curr);
				return true;
			}
		}
		else {
			// Node has 2 children
			if (find(k, pred, predOp, replace, replaceOp, curr)==ABORT || curr->op!=currOp) continue;
			relocOp = new RelocateOp(curr, currOp, k, replace->key);
			if (replace->op.compare_exchange_strong(replaceOp,FLAG(relocOp, RELOCATE))) {
				if (helpRelocate(relocOp, pred, predOp, replace)) return true;
			}
		}
	}
}

bool NonBlockingBST ::  helpRelocate(Operation* op, Node* pred, Operation* predOp, Node* curr)
{
	RelocateOp* op_p = reinterpret_cast<RelocateOp*>(op);
	int seenState = op_p->state;
	if (seenState == ONGOING) {
		Operation* seenOp = reinterpret_cast<Operation*>(__sync_val_compare_and_swap(reinterpret_cast<long*>(&op_p->dest->op),
				                                  reinterpret_cast<long>(op_p->destOp), 
								  reinterpret_cast<long>(FLAG(op, RELOCATE))));
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
		op_p->dest->key.compare_exchange_strong(op_p->removeKey, op_p->replaceKey);
		Operation* tmp = FLAG(op, RELOCATE);
		op_p->dest->op.compare_exchange_strong(tmp, FLAG(op, NONE));
	}
	bool result = (seenState == SUCCESSFUL);
	if (op_p->dest == curr) return result;
	Operation* tmp = FLAG(op, RELOCATE);
	curr->op.compare_exchange_strong(tmp, FLAG(op, result?MARK:NONE));
	if (result) {
		if (op_p->dest == pred) predOp = FLAG(op, NONE);
		helpMarked(pred, predOp, curr);
	}
	return result;
}

void NonBlockingBST :: helpMarked(Node* pred, Operation* predOp, Node* curr)
{
	Node* newRef;
	if (ISNULL(curr->left.load())) {
		if (ISNULL(curr->right.load()))
			newRef = SETNULL(curr);
		else
			newRef = curr->right;
	}
	else    newRef = curr->left;
	Operation* casOp = new ChildCASOp(curr == pred->left, curr, newRef);
	if (pred->op.compare_exchange_strong(predOp, FLAG(casOp, CHILDCAS)))
		helpChildCAS(casOp, pred);
}

void NonBlockingBST :: help(Node* pred, Operation* predOp, Node* curr, Operation* currOp)
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
