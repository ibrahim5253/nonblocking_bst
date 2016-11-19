#include <iostream>
#include <atomic>
#include <algorithm>  // for max()
#include <climits>

using namespace std;

enum State {CLEAN, DFLAG, IFLAG, MARK};
#define INF2 INT_MAX
#define INF1 INT_MAX-1

#define UPDATE(f,op)   (reinterpret_cast<long>(op) | f)
#define STATE(u)       (u&3)
#define IINFO(u)       ((IInfo*)(u&(~3)))
#define DINFO(u)       ((DInfo*)(u&(~3)))

// two lsb(state), rest pointer to Info
typedef atomic<long> Update;

class Info
{
public:
   Info() {}
};

class Node
{
public:
   int key;
   bool isLeaf;
   Update update;
   Node *left, *right;

   Node(){
      this->update.store(CLEAN);
   }

   Node(int k,bool isLeaf) {
      this->isLeaf = isLeaf;
      this->key = k;
   }
};

class IInfo : public Info
{
public:
   Node *p, *newInternal;
   Node *l;

   IInfo() {}

   IInfo(Node *p, Node *newInternal, Node *l) {
      this->p = p;
      this->newInternal = newInternal;
      this->l = l;
   }
};

class DInfo : public Info
{
public:
   Node *gp, *p;
   Node *l;
   Update pupdate;

   DInfo() {}

   DInfo(DInfo &c) {
      gp = c.gp; p = c.p; l = c.l;
      pupdate.store(c.pupdate);
   }
};

// return value from Search operation
class SearchRes
{
public:
   Node *gp, *p;
   Node *l;
   Update gpupdate, pupdate;

   SearchRes() {}

   SearchRes(SearchRes &c) {
      gp = c.gp; p = c.p; l = c.l;
      gpupdate.store(c.gpupdate);
      pupdate.store(c.pupdate);
   }

   void operator =(const SearchRes &c) {
      gp = c.gp; p = c.p; l = c.l;
      gpupdate.store(c.gpupdate);
      pupdate.store(c.pupdate);
   }
};

Node *Root = new Node(INF2,false);

void init_root(Node *Root)
{
   Root->update.store(CLEAN);
   Root->left = new Node(INF1, true);
   Root->right = new Node(INF2, true);
}

/************************************************
 *             Tree Operations
 ***********************************************/
void HelpInsert(IInfo *op);
void Help(Update &u);
void HelpMarked(DInfo *op);
bool HelpDelete(DInfo *op);
void CAS_Child(Node *parent, Node *old, Node *newInternal);

SearchRes Search(int k)
{
   SearchRes ret;
   Node *gp, *p;
   Node *l = Root;
   Update gpupdate, pupdate;

   while (l->isLeaf == false) {
      gp = p;
      p = l;
      gpupdate.store(pupdate);
      pupdate.store(p->update);
      if(k < l->key)
         l = p->left;
      else
         l = p->right;
   }

   ret.gp=gp; ret.p=p; ret.l=l;
   ret.gpupdate.store(gpupdate); ret.pupdate.store(pupdate);

   return ret;
}

Node *Find(int k)
{
   Node *l = Search(k).l;
   if(l->key == k)
      return l;
   else
      return NULL;
}

bool Insert(int k)
{
   Node *p, *newInternal;
   Node *l, *newSibling;
   Node *newLeaf = new Node(k, true);
   Update pupdate, result;
   IInfo *op;
   SearchRes res;

   while (true) {
      res = Search(k);
      p = res.p; l = res.l; pupdate.store(res.pupdate);
      if (l->key == k)
         return false;

      if (STATE(pupdate) != CLEAN) {
         Help(pupdate);
      }
      else {
         newSibling = new Node(l->key, true);
         newInternal = new Node(max(l->key,k), false);
         if(newLeaf->key < newSibling->key) {
            newInternal->left = newLeaf;
            newInternal->right = newSibling;
         }
         else {
            newInternal->left = newSibling;
            newInternal->right = newLeaf;
         }
         op = new IInfo(p, newInternal, l);
         result = __sync_val_compare_and_swap(reinterpret_cast<long*>(&(p->update)), pupdate, UPDATE(IFLAG,op));
         if (result == pupdate) {
            HelpInsert(op);
            return true;
         }
         else {
            Help(result);
         }
      }
   }
}

void HelpInsert(IInfo *op)
{
   CAS_Child(op->p,op->l,op->newInternal);
   __sync_bool_compare_and_swap(reinterpret_cast<long*>(&(op->p->update)),UPDATE(IFLAG,op),UPDATE(CLEAN,op));
}

bool Delete(int k)
{
   Node *gp, *p;
   Node *l;
   Update pupdate,gpupdate,result;
   DInfo *op;
   SearchRes res;

   while (true) {
      res = Search(k);
      gp = res.gp; p = res.p; l = res.l;
      pupdate.store(res.pupdate);
      gpupdate.store(res.gpupdate);

      if (l->key != k)
         return false;
      if (STATE(gpupdate) != CLEAN) {
         Help(gpupdate);
      }
      else if (STATE(pupdate) != CLEAN) {
         Help(pupdate);
      }
      else {
         op = new DInfo;
         op->gp = gp; op->p = p; op->l = l;
         op->pupdate.store(pupdate);
         result = __sync_val_compare_and_swap(reinterpret_cast<long*>(&(gp->update)),gpupdate,UPDATE(DFLAG,op));
         if (result == gpupdate) {
            if (HelpDelete(op))
               return true;
         }
         else {
            Help(result);
         }
      }
   }
}

bool HelpDelete(DInfo *op)
{
   Update result;

   result = __sync_val_compare_and_swap(reinterpret_cast<long*>(&(op->p->update)),op->pupdate,UPDATE(MARK,op));
   if (result == op->pupdate || result == UPDATE(MARK,op)) {
      HelpMarked(op);
      return true;
   }
   else {
      Help(result);
      __sync_bool_compare_and_swap(reinterpret_cast<long*>(&(op->gp->update)),UPDATE(DFLAG,op),UPDATE(CLEAN,op));
      return false;
   }
}

void HelpMarked(DInfo *op)
{
   Node *other;

   if (op->p->right == op->l)
      other = op->p->left;
   else
      other = op->p->right;

   CAS_Child(op->gp,op->p,other);
   __sync_bool_compare_and_swap(reinterpret_cast<long*>(&(op->gp->update)),UPDATE(DFLAG,op),UPDATE(CLEAN,op));
}

void Help(Update &u)
{
   if (STATE(u) == IFLAG) {
      HelpInsert(IINFO(u));
   }
   else if (STATE(u) == MARK) {
      HelpMarked(DINFO(u));
   }
   else if (STATE(u) == DFLAG) {
      HelpDelete(DINFO(u));
   }
}

void CAS_Child(Node *parent, Node *old, Node *newInternal)
{
   if (newInternal->key < parent->key)
      __sync_bool_compare_and_swap(reinterpret_cast<long*>(&(parent->left)),reinterpret_cast<long>(old),reinterpret_cast<long>(newInternal));
   else
      __sync_bool_compare_and_swap(reinterpret_cast<long*>(&(parent->right)),reinterpret_cast<long>(old),reinterpret_cast<long>(newInternal));
}

int main()
{
   init_root(Root);
   Insert(10);
   Insert(20);
   Delete(20);
   if(Find(20))
      cout << "fu\n";
   return 0;
}
