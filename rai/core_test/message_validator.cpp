#include <gtest/gtest.h>
#include <iostream>
#include <array>
#include <vector>

using namespace std;
#include <rai/consensus/delegate_key_store.hpp>
#include <rai/consensus/message_validator.hpp>

using KeyStores = DelegateKeyStore *;
using Nodes = vector<MessageValidator>;//, NUM_DELEGATES>;
using SigVec = vector<MessageValidator::DelegateSignature>;

//create the nodes and distribute the public keys
Nodes setup_nodes()
{
	static std::mutex setup_nodes_mutex;
	static bool inited = false;
	static Nodes nodes;

	std::lock_guard<std::mutex> lock(setup_nodes_mutex);
	if(! inited )
	{
		bls::init();
		static DelegateKeyStore dks[NUM_DELEGATES];

		vector<PublicKey> pkeys;
		for(int i = 0; i < NUM_DELEGATES; ++i)
		{
	        nodes.push_back(MessageValidator(i, dks[i]));
			pkeys.push_back(nodes[i].GetPublicKey());
		}

		// everyone gets everyone's public key, including itself's
		for(int i = 0; i < NUM_DELEGATES; ++i)
		{
			auto & ks = dks[i];
			for(int k = 0; k < NUM_DELEGATES; ++k)
			{
				ks.OnPublicKey(k, pkeys[k]);
			}
		}
		inited = true;
	}
	return nodes;
}

TEST (message_validator, single_sig)
{
	auto nodes = setup_nodes();

	PrePrepareMessage preprepare;
	PrepareMessage prepare(preprepare.timestamp);
	CommitMessage commit(preprepare.timestamp);

	for(uint8_t i = 0; i < NUM_DELEGATES; ++i)
	{
		auto & validator = nodes[i];
		validator.Sign(preprepare);
		ASSERT_TRUE(validator.Validate(preprepare, i));
		preprepare.timestamp++;
		ASSERT_FALSE(validator.Validate(preprepare, i));

		validator.Sign(prepare);
		ASSERT_TRUE(validator.Validate(prepare, i));
		prepare.timestamp++;
		ASSERT_FALSE(validator.Validate(prepare, i));

		validator.Sign(commit);
		ASSERT_TRUE(validator.Validate(commit, i));
		commit.timestamp++;
		ASSERT_FALSE(validator.Validate(commit, i));
	}
}

TEST (message_validator, consensus_round)
{
	auto nodes = setup_nodes();

	//step 1, preprepare
	//primary, node[0], signs the preprepare
	PrePrepareMessage preprepare;
	{
		auto & validator = nodes[0];
		validator.Sign(preprepare);
		ASSERT_TRUE(validator.Validate(preprepare, 0));
	}

	//step 2, prepare
	//backups (and primary, TODO reuse signature of PrePrepareMessage?)
	//verify the signature of preprepare and create signed prepares
	vector<PrepareMessage> prepares;
	for(int i = 0; i < NUM_DELEGATES; ++i)
	{
		auto & validator = nodes[i];
		ASSERT_TRUE(validator.Validate(preprepare, 0));

		prepares.push_back(PrepareMessage(preprepare.timestamp));
		auto & msg = prepares.back();
		validator.Sign(msg);
		ASSERT_TRUE(validator.Validate(msg, i));
	}

	//step 3, postprepare
	//primary verify the prepares and aggregate the signatures
	PostPrepareMessage postprepare(preprepare.timestamp);
	{
		auto & validator = nodes[0];
		SigVec signatures(NUM_DELEGATES);
		for(int i = 0; i < NUM_DELEGATES; ++i)
		{
			auto & msg = prepares[i];
			ASSERT_TRUE(validator.Validate(msg, i));
			signatures[i].delegate_id = i;
			signatures[i].signature = msg.signature;
		}
		ASSERT_TRUE(validator.Sign(postprepare, signatures));
		ASSERT_TRUE(validator.Validate(postprepare, prepares[0]));
	}

	//step 4, commit
	//delegates verify the signature of postprepare and create signed commits
	vector<CommitMessage> commits;
	for(int i = 0; i < NUM_DELEGATES; ++i)
	{
		auto & validator = nodes[i];
		ASSERT_TRUE(validator.Validate(postprepare, prepares[i]));

		commits.push_back(CommitMessage(preprepare.timestamp));
		auto & msg = commits.back();
		validator.Sign(msg);
		ASSERT_TRUE(validator.Validate(msg, i));
	}

	//step 5, postcommit
	//primary verify the commits and aggregate the signatures
	PostCommitMessage postcommit(preprepare.timestamp);
	{
		auto & validator = nodes[0];
		SigVec signatures(NUM_DELEGATES);
		for(int i = 0; i < NUM_DELEGATES; ++i)
		{
			auto & msg = commits[i];
			ASSERT_TRUE(validator.Validate(msg, i));
			signatures[i].delegate_id = i;
			signatures[i].signature = msg.signature;
		}
		ASSERT_TRUE(validator.Sign(postcommit, signatures));
		ASSERT_TRUE(validator.Validate(postcommit, commits[0]));
	}

	//step 6, propagation
	//delegates verify the signature of postcommit
	for(int i = 0; i < NUM_DELEGATES; ++i)
	{
		auto & validator = nodes[i];
		ASSERT_TRUE(validator.Validate(postcommit, commits[i]));
	}
}

#define LOOPS   10
TEST (message_validator, signature_order)
{
	auto nodes = setup_nodes();

	PrePrepareMessage preprepare;
	{
		auto & validator = nodes[0];
		validator.Sign(preprepare);
	}
	vector<PrepareMessage> prepares;
	for(int i = 0; i < NUM_DELEGATES; ++i)
	{
		auto & validator = nodes[i];

		prepares.push_back(PrepareMessage(preprepare.timestamp));
		auto & msg = prepares.back();
		validator.Sign(msg);
	}

	for(int r = 0; r < LOOPS; ++r)
	{
		//Now the primary have prepares. It aggregates 2/3*32 signatures with random order.
		PostPrepareMessage postprepare(preprepare.timestamp);
		{
			auto & validator = nodes[0];
			SigVec signatures(NUM_DELEGATES);
			for(int i = 0; i < NUM_DELEGATES; ++i)
			{
				auto & msg = prepares[i];
				ASSERT_TRUE(validator.Validate(msg, i));
				signatures[i].delegate_id = i;
				signatures[i].signature = msg.signature;
			}

			std::random_shuffle ( signatures.begin(), signatures.end() );
			signatures.resize(NUM_DELEGATES*2/3);

			SigVec signatures_copy;
			for(auto &xx : signatures)
			{
				signatures_copy.push_back(xx);
			}

			ASSERT_TRUE(validator.Sign(postprepare, signatures));
		}
		//All delegate verify the aggregated signature
		for(int i = 0; i < NUM_DELEGATES; ++i)
		{
			auto & validator = nodes[i];
			ASSERT_TRUE(validator.Validate(postprepare, prepares[i]));
		}
	}
}

TEST (message_validator, signature_order_temp)
{
    auto nodes = setup_nodes();

    PrePrepareMessage preprepare;
    {
        auto & validator = nodes[0];
        validator.Sign(preprepare);
    }

    //step 4, commit
    //delegates verify the signature of postprepare and create signed commits
    vector<CommitMessage> commits;
    for(int i = 0; i < NUM_DELEGATES; ++i)
    {
        auto & validator = nodes[i];

        commits.push_back(CommitMessage(preprepare.timestamp));
        auto & msg = commits.back();
        validator.Sign(msg);
        ASSERT_TRUE(validator.Validate(msg, i));
    }

    //step 5, postcommit
    //primary verify the commits and aggregate the signatures
    PostCommitMessage postcommit(preprepare.timestamp);
    {
        auto & validator = nodes[0];
        SigVec signatures(NUM_DELEGATES);
        for(int i = 0; i < NUM_DELEGATES; ++i)
        {
            auto & msg = commits[i];
            ASSERT_TRUE(validator.Validate(msg, i));
            signatures[i].delegate_id = i;
            signatures[i].signature = msg.signature;
        }
        ASSERT_TRUE(validator.Sign(postcommit, signatures));
        ASSERT_TRUE(validator.Validate(postcommit, commits[0]));
    }
}
