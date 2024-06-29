#include <iostream>
#include <set>
#include <vector>
#include <algorithm>
std::vector<std::pair<int, int>> generateSackList(const std::set<int>& receivedPackets, int lastAck) {
    std::vector<std::pair<int, int>> sackList;
    auto it = receivedPackets.upper_bound(lastAck);
    while (it != receivedPackets.end()) {
        int start = *it;
        int end = start + 4;
        // Find the end of the contiguous block
        auto nextIt = std::next(it);
        while (nextIt != receivedPackets.end() && *nextIt == end) {
            end += 4;
            nextIt = std::next(nextIt);
        }
        sackList.emplace_back(start, end);
        it = nextIt;
    }
    return sackList;
}

void test(){
    std::vector<int> a = {1, 2};
    std::cout << "aaaa\n";
    a.at(3);
    std::cout << "bbbb\n";
}

int main() {
    std::set<int> receivedPackets;
    int inputSeqNum;
    int lastAck = 1;
    test();

    
    return 0;
}