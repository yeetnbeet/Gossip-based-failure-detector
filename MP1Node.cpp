/**********************************
 * FILE NAME: MP1Node.cpp
 *
 * DESCRIPTION: Membership protocol run by this Node.
 * 				Definition of MP1Node class functions.
 **********************************/

#include "MP1Node.h"

/*
 * Note: You can change/add any functions in MP1Node.{h,cpp}
 */

/**
 * Overloaded Constructor of the MP1Node class
 * You can add new members to the class if you think it
 * is necessary for your logic to work
 */
MP1Node::MP1Node(Member *member, Params *params, EmulNet *emul, Log *log,
        Address *address) : emulNet(emul), log(log), par(params), memberNode(member) {
	for( int i = 0; i < 6; i++ ) {
		NULLADDR[i] = 0;
	}
	this->memberNode->addr = *address;
}

/**
 * Destructor of the MP1Node class
 */
MP1Node::~MP1Node() {}

/**
 * FUNCTION NAME: recvLoop
 *
 * DESCRIPTION: This function receives message from the network and pushes into the queue
 * 				This function is called by a node to receive messages currently waiting for it
 */
int MP1Node::recvLoop() {
    if ( memberNode->bFailed ) {
    	return false;
    }
    else {
    	return emulNet->ENrecv(&(memberNode->addr), enqueueWrapper, NULL, 1, &(memberNode->mp1q));
    }
}

/**
 * FUNCTION NAME: enqueueWrapper
 *
 * DESCRIPTION: Enqueue the message from Emulnet into the queue
 */
int MP1Node::enqueueWrapper(void *env, char *buff, int size) {
	Queue q;
	return q.enqueue((queue<q_elt> *)env, (void *)buff, size);
}

/**
 * FUNCTION NAME: nodeStart
 *
 * DESCRIPTION: This function bootstraps the node
 * 				All initializations routines for a member.
 * 				Called by the application layer.
 */
void MP1Node::nodeStart(char *servaddrstr, short servport) {
    Address joinaddr;
    joinaddr = getJoinAddress();

    // Self booting routines
    if( initThisNode(&joinaddr) == -1 ) {
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "init_thisnode failed. Exit.");
#endif
        exit(1);
    }

    if( !introduceSelfToGroup(&joinaddr) ) {
        finishUpThisNode();
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Unable to join self to group. Exiting.");
#endif
        exit(1);
    }

    return;
}

/**
 * FUNCTION NAME: initThisNode
 *
 * DESCRIPTION: Find out who I am and start up
 */
int MP1Node::initThisNode(Address *joinaddr) {
	/*
	 * This function is partially implemented and may require changes
	 */

	memberNode->bFailed = false;
	memberNode->inited = true;
	memberNode->inGroup = false;
    // node is up!
	memberNode->nnb = 0;
	memberNode->heartbeat = 0;
	memberNode->pingCounter = TFAIL;
	memberNode->timeOutCounter = -1;
    initMemberListTable(memberNode);

    return 0;
}

/**
 * FUNCTION NAME: introduceSelfToGroup
 *
 * DESCRIPTION: Join the distributed system
 */
int MP1Node::introduceSelfToGroup(Address *joinaddr) {
#ifdef DEBUGLOG
    static char s[1024];
#endif

    if ( 0 == memcmp((char *)&(memberNode->addr.addr), (char *)&(joinaddr->addr), sizeof(memberNode->addr.addr))) {
        // I am the group booter (first process to join the group). Boot up the group
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Starting up group...");
#endif
        memberNode->inGroup = true;
    }
    else {
        // create JOINREQ message: format of data is {struct Address myaddr}
        sendMemberListToNode_(joinaddr, MsgTypes::JOINREQ, memberNode->memberList);
#ifdef DEBUGLOG
        sprintf(s, "Trying to join...");
        log->LOG(&memberNode->addr, s);
#endif
    }

    return 1;

}

/**
 * FUNCTION NAME: finishUpThisNode
 *
 * DESCRIPTION: Wind up this node and clean up state
 */
int MP1Node::finishUpThisNode(){
   /*
    * Your code goes here
    */
    memberNode->memberList.clear();
    memberNode->inGroup = false;
    return 0;
}

/**
 * FUNCTION NAME: nodeLoop
 *
 * DESCRIPTION: Executed periodically at each member
 * 				Check your messages in queue and perform membership protocol duties
 */
void MP1Node::nodeLoop() {
    if (memberNode->bFailed) {
    	return;
    }

    // Check my messages
    checkMessages();

    // Wait until you're in the group...
    if( !memberNode->inGroup ) {
    	return;
    }

    // ...then jump in and share your responsibilites!
    nodeLoopOps();

    return;
}

/**
 * FUNCTION NAME: checkMessages
 *
 * DESCRIPTION: Check messages in the queue and call the respective message handler
 */
void MP1Node::checkMessages() {
    void *ptr;
    int size;

    // Pop waiting messages from memberNode's mp1q
    while ( !memberNode->mp1q.empty() ) {
    	ptr = memberNode->mp1q.front().elt;
    	size = memberNode->mp1q.front().size;
    	memberNode->mp1q.pop();
    	recvCallBack((char *)ptr, size);
    }
    return;
}

/**
 * FUNCTION NAME: recvCallBack
 *
 * DESCRIPTION: Message handler for different message types
 */
bool MP1Node::recvCallBack(char *data, int size ) {
	/*
	 * Your code goes here
	 */
    MessageHdr* recv_msg = (MessageHdr*) data;
    if(recv_msg->msgType == MsgTypes::JOINREQ) {
        addMembers_(data);
        sendMemberListToNode_(&recv_msg->addr, MsgTypes::JOINREP,
                memberNode->memberList);
    } else if(recv_msg->msgType == MsgTypes::JOINREP) {
        addMembers_(data);
        memberNode->inGroup = true;
    } else if(recv_msg->msgType == MsgTypes::GOSSIP) {
        addMembers_(data);
    } else {
        // For Dummy Message? future false use
        return false;
    }
    
    free(data);
    return true;
}

void MP1Node::addMembers_(char* data) {
    MessageHdr* recv_msg = (MessageHdr*) data;
    MemberListEntry * members = (MemberListEntry*)(recv_msg+1);
    for(int i = 0; i < recv_msg->numElements; ++i) {
        int member_id = members[i].getid();
        short member_port = members[i].getport();
        long member_beat = members[i].getheartbeat();
        
        auto found = findMember_(member_id, member_port);
        // Member not found, add entry with latest heartbeat and current time
        if(found == memberNode->memberList.end()) {
            memberNode->memberList.emplace_back(member_id, member_port,
                    member_beat, par->getcurrtime());
            logAdd_(member_id, member_port);
        }
        else {
            //Member found
            //Check if heartbeat is new and update time, otherwise ignore
            if(found->getheartbeat() < member_beat) {
                found->setheartbeat(member_beat);
                found->settimestamp(par->getcurrtime());
            }
        
        }
    }

}

vector<MemberListEntry>::iterator MP1Node::findMember_(int id, short port) {
    auto func_comp = [id, port](MemberListEntry &m1) {
        return (m1.getid() == id && m1.getport() == port);
    };
    auto found = std::find_if(memberNode->memberList.begin(),
            memberNode->memberList.end(), func_comp);
   
    return found;
}

void MP1Node::sendMemberListToNode_(Address *recv_addr, MsgTypes msgtype,
        std::vector<MemberListEntry>& memberVector) {
    auto my_members_size = memberVector.size();
    size_t msgsize = sizeof(MessageHdr) + my_members_size*(sizeof(MemberListEntry));
    MessageHdr * msg = (MessageHdr*) malloc(msgsize* sizeof(char));
    char *data = (char *) (msg+1);
    msg->msgType = msgtype;
    msg->addr = memberNode->addr;
    msg->numElements = my_members_size;

    if(msg->numElements > 0) {
        memcpy(data, memberVector.data(), msg->numElements *
                sizeof(MemberListEntry));
    }

    emulNet->ENsend(&memberNode->addr, recv_addr, (char *)msg,
            msgsize);
    free(msg);
}


/**
 * FUNCTION NAME: nodeLoopOps
 *
 * DESCRIPTION: Check if any node hasn't responded within a timeout period and then delete
 * 				the nodes
 * 				Propagate your membership list
 */
void MP1Node::nodeLoopOps() {

    // inc self heartbeat
    auto myEntry = memberNode->memberList.begin();
    myEntry->setheartbeat(myEntry->getheartbeat()+1);
    myEntry->settimestamp(par->getcurrtime());

    // If TREMOVE eliminate entries
    auto it = myEntry;
    while(it != memberNode->memberList.end()) {
        if((it->gettimestamp() + TREMOVE) < par->getcurrtime()) {
            logRemove_(it->getid(), it->getport());
            it = memberNode->memberList.erase(it);
        } else {
            ++it;
        }
    }
    // If TFAIL dont send to that entries and pop-up valid entries
    std::vector<MemberListEntry> available_entries;
    auto func_include = [this](MemberListEntry & m) {
        return ((m.gettimestamp() + TFAIL) >= par->getcurrtime());
    };
    std::copy_if(memberNode->memberList.begin(),
            memberNode->memberList.end(), std::back_inserter(available_entries),
            func_include);

    // Send to O(logn) entries
    if(available_entries.size() > 3) {
        std::vector<MemberListEntry> to_send;   
        std::vector<MemberListEntry> random_pool(available_entries);
        uint64_t num_members_to_send = log2(random_pool.size()-1);
        while(to_send.size() < num_members_to_send) {
            int index = (rand() % (random_pool.size()-1)) +1;
            to_send.push_back(random_pool[index]);
            random_pool.erase(random_pool.begin()+index);
        }

        for(auto & entry : to_send) {
            auto addr_to_send = genAddress_(entry.getid(),
                    entry.getport());
            sendMemberListToNode_(&addr_to_send, MsgTypes::GOSSIP,
                available_entries); 
        }
    }
    else {    
        auto addr_to_send = genAddress_(available_entries[1].getid(),
                available_entries[1].getport());
        sendMemberListToNode_(&addr_to_send, MsgTypes::GOSSIP, available_entries); 
    }
    return;
}

/**
 * FUNCTION NAME: isNullAddress
 *
 * DESCRIPTION: Function checks if the address is NULL
 */
int MP1Node::isNullAddress(Address *addr) {
	return (memcmp(addr->addr, NULLADDR, 6) == 0 ? 1 : 0);
}

void MP1Node::logAdd_(int id, short port) {
    Address to_add = genAddress_(id, port);
    log->logNodeAdd(&memberNode->addr, &to_add);
}

void MP1Node::logRemove_(int id, short port) {
    Address to_remove = genAddress_(id, port);
    log->logNodeRemove(&memberNode->addr, &to_remove);
}

Address MP1Node::genAddress_(int id, short port) {
    Address to_add;
    *(int*)(&to_add.addr) = id;
    *(char*)(&to_add.addr[4]) = port;
    return to_add;
}

/**
 * FUNCTION NAME: getJoinAddress
 *
 * DESCRIPTION: Returns the Address of the coordinator
 */
Address MP1Node::getJoinAddress() {
    Address joinaddr;

    memset(&joinaddr, 0, sizeof(Address));
    *(int *)(&joinaddr.addr) = 1;
    *(short *)(&joinaddr.addr[4]) = 0;

    return joinaddr;
}

/**
 * FUNCTION NAME: initMemberListTable
 *
 * DESCRIPTION: Initialize the membership list
 */
void MP1Node::initMemberListTable(Member *memberNode) {
	memberNode->memberList.clear();
        int my_id = *(int*)(&memberNode->addr.addr);
        short my_port = *(char*)(&memberNode->addr.addr[4]);
        memberNode->memberList.emplace_back(my_id, my_port, 0, par->getcurrtime());
        log->logNodeAdd(&memberNode->addr, &memberNode->addr);
}

/**
 * FUNCTION NAME: printAddress
 *
 * DESCRIPTION: Print the Address
 */
void MP1Node::printAddress(Address *addr)
{
    printf("%d.%d.%d.%d:%d \n",  addr->addr[0],addr->addr[1],addr->addr[2],
                                                       addr->addr[3], *(short*)&addr->addr[4]) ;    
}
