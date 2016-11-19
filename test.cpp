#include "bst.h"
#include <random>
#include <thread>
#include <vector>
#include <chrono>

NonBlockingBST B; //Shared tree

struct message {
	int tid;
	int req;
	int val;
	string req_t, res_t;
	bool res;
	
	message(int t, int r, int v, string rq_t, string rs_t, bool stat) :
		tid(t), req(r), val(v), req_t(rq_t), res_t(rs_t), res(stat) {}
};

vector<message> vec;

void get_time(time_t timer, char timestr[100]) 
{
	struct tm *t = localtime(&timer);
	strftime(timestr, 100, "%T", t);
}

double insert_time = 0, del_time = 0;

void run(int tid, int op, int k, int lmbda)
{
	unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
	default_random_engine generator(seed);
	exponential_distribution<double> dis(1.0/lmbda);

	char t1[100], t2[100];
	for (int i=0; i<k; ++i) {
		int val = rand()%10;

		auto req_t = std::chrono::high_resolution_clock::now();
	        get_time(chrono::system_clock::to_time_t(req_t), t1);

		bool stat = op==1?B.add(val):B.remove(val);	

		auto res_t = std::chrono::high_resolution_clock::now();
	        get_time(chrono::system_clock::to_time_t(res_t), t2);

		(op==1?insert_time:del_time) += chrono::duration_cast<std::chrono::microseconds>(res_t-req_t).count();
		vec.push_back(message(tid, op, val, t1, t2, stat));
		
		this_thread::sleep_for(std::chrono::milliseconds((int)dis(generator)));
	}
}

int main(int argc,char** argv)
{
	int n_i = atoi(argv[1]);
	int n_d = atoi(argv[2]);
	
	int lmbda_i = 100;
	int lmbda_d = 120;
	
	vector<thread> inserters, deleters;

	for (int i=0; i<n_i; ++i)
		inserters.push_back(thread(run, i+1, 1, 20, lmbda_i));

	for (int i=0; i<n_d; ++i)
		deleters.push_back(thread(run, n_i+i+1, 2, 20, lmbda_d));
	
	for (auto &t: inserters)
		t.join();

	for (auto &t: deleters)
		t.join();

	cout << "Thread\tReq\tval\tReq_t\t\tStatus\tRes_t\n";
	for (message &m : vec)
		cout << m.tid << "\t" << (m.req==1?"ADD":"DEL") << "\t" << m.val << "\t" <<
			m.req_t << "\t" << m.res << "\t" << m.res_t << "\n";
	return 0;
}

