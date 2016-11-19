#include "bst.h"

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

