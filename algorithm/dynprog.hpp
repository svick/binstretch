#ifndef _DYNPROG_H
#define _DYNPROG_H 1

#include <chrono>
#include <algorithm>

#include "common.hpp"
#include "fits.hpp"
#include "measure.hpp"

// which Test procedure are we using
#define TEST sparse_dynprog_test2

void print_tuple(const int* tuple)
{
    fprintf(stderr, "(");
    bool first = true;
    for(int i=0; i<BINS; i++)
    {
	if(!first) {
	    fprintf(stderr, ",");
	}	
	first=false;
	fprintf(stderr, "%d", tuple[i]);
    }
    fprintf(stderr, ")\n");
}
// encodes BINS numbers less than S into a single number
uint64_t encodetuple(const int *tuple, int pos)
{
    if(pos == BINS-1)
    {
	return tuple[pos];
    } else {
	return encodetuple(tuple, pos+1)*(S+1) + tuple[pos];
    }
}

// decodes BINS numbers less than 1024 from a single number
// allocation of new_tuple needs to be done beforehand
void decodetuple(int *new_tuple, uint64_t source, int pos)
{
    assert(new_tuple != NULL);
    
    new_tuple[pos] = source % (S+1);
    int new_source = source / (S+1);
    
    if (pos == BINS-1)
    {
	return;
    } else {
        decodetuple(new_tuple, new_source, pos+1);
    }
}

// solving using dynamic programming, sparse version, starting with empty instead of full queue

// global binary array of feasibilities used for sparse_dynprog_alternate_test
//int *F;
//int *oldqueue;
//int *newqueue;

#define DEFAULT_DP_SIZE 100000

void dynprog_attr_init(thread_attr *tat)
{
    assert(tat != NULL);
    //tat->F = new std::vector<int>(BINARRAY_SIZE, 0);
    tat->oldqueue = new std::vector<uint64_t>();
    tat->oldqueue->reserve(DEFAULT_DP_SIZE);
    tat->newqueue = new std::vector<uint64_t>();
    tat->newqueue->reserve(DEFAULT_DP_SIZE);
}

void dynprog_attr_free(thread_attr *tat)
{
    delete tat->oldqueue;
    delete tat->newqueue;
    //delete F;
}

/* New dynprog test with some minor improvements. */
bool sparse_dynprog_test2(const binconf *conf, thread_attr *tat)
{
    // binary array of feasibilities
    // int f[S+1][S+1][S+1] = {0};
    // int *F = tat->F;
    //std::vector<uint64_t> *oldqueue = tat->oldqueue;
    //std::vector<uint64_t> *newqueue = tat->newqueue;

    tat->newqueue->clear();
    tat->oldqueue->clear();
    std::vector<uint64_t> *poldq;
    std::vector<uint64_t> *pnewq;
    std::vector<uint64_t> *swapper;

    int *tuple; tuple = (int *) calloc(BINS, sizeof(int));
    
    poldq = tat->oldqueue;
    pnewq = tat->newqueue;
    
    
    int phase = 0;

    uint64_t index;
    for (int size=S; size>2; size--)
    {
	int k = conf->items[size];
	while (k > 0)
	{
	    phase++;
	    if (phase == 1) {
		
		tuple[0] = size;
		index = encodetuple(tuple, 0);
		pnewq->push_back(index);
		//tat->F[index] = 1;
	    } else {
		for(int i=0; i < poldq->size(); i++)
		{
		    index = (*poldq)[i];

		    /* Instead of having a global array of feasibilities, we now sort the poldq array. */
		    if (i >= 1)
		    {
			if (index == (*poldq)[i-1])
			{
			    continue;
			}
		    }
		    
		    decodetuple(tuple,index,0);
		    //int testindex = encodetuple(tuple,0);
		    //assert(testindex == index);
		    
		    //tat->F[index] = 0;
		    
		    // try and place the item
		    for(int i=0; i < BINS; i++)
		    {
			// same as with Algorithm, we can skip when sequential bins have the same load
			if (i > 0 && tuple[i] == tuple[i-1])
			{
			    continue;
			}
			
			if(tuple[i] + size > S) {
			    continue;
			}
			
			tuple[i] += size;
			int from = sortarray_one_increased(&tuple, i);
			uint64_t newindex = encodetuple(tuple,0);

			// debug assertions
			if( ! (newindex <= BINARRAY_SIZE) && (newindex >= 0))
			{
			    fprintf(stderr, "Tuple and index %" PRIu64 " are weird.\n", newindex);
			    print_tuple(tuple);
			    exit(-1);
			}
			
			/* if( tat->F[newindex] != 1)
			{
			    tat->F[newindex] = 1;
			}
			*/
			pnewq->push_back(newindex);

			tuple[from] -= size;
			sortarray_one_decreased(&tuple, from);
		    }
		}
		if (pnewq->size() == 0) {
		    free(tuple);
		    return false;
		}
	    }

	    // swap queues
	    swapper = pnewq; pnewq = poldq; poldq = swapper;
	    // sort the old queue
	    sort(poldq->begin(), poldq->end()); 
	    pnewq->clear();
	    k--;
	}

    }

    /* Heuristic: solve the cases of sizes 2 and 1 without generating new
       configurations. */
    for (int i=0; i < poldq->size(); i++)
    {
	index = (*poldq)[i];
	/* Instead of having a global array of feasibilities, we now sort the poldq array. */
	if (i >= 1)
	{
	    if (index == (*poldq)[i-1])
	    {
		continue;
	    }
	}

	decodetuple(tuple,index,0);
	//int testindex = encodetuple(tuple,0);
	int free_size = 0, free_for_twos = 0;
	for (int i=0; i<BINS; i++)
	{
	    free_size += (S - tuple[i]);
	    free_for_twos += (S - tuple[i])/2;
	}
	
	if ( free_size < conf->items[1] + 2*conf->items[2])
	{
	    continue;
	}

	if (free_for_twos >= conf->items[2])
	{
	    free(tuple);
	    return true;
	}
    }

    free(tuple);
    return false;
}

// a wrapper that hashes the new configuration and if it is not in cache, runs TEST
// it edits h but should return it to original state (due to Zobrist hashing)
bool hash_and_test(binconf *h, int item, thread_attr *tat)
{
#ifdef MEASURE
    tat->test_counter++;
#endif
    
    bool feasible;
    int hashedvalue;
    
    h->items[item]++;
    dp_rehash(h,item);
    
    hashedvalue = dp_hashed(h, tat);
    if (hashedvalue != -1)
    {
	DEEP_DEBUG_PRINT("Found a cached value of hash %llu in dynprog instance: %d.\n", h->itemhash, hashedvalue);
	feasible = (bool) hashedvalue;
    } else {
	DEEP_DEBUG_PRINT("Nothing found in dynprog cache for hash %llu.\n", h->itemhash);
	feasible = TEST(h, tat);
	// temporary sanity check
	// assert(feasible == sparse_dynprog_test(h,tat));
	DEEP_DEBUG_PRINT("Pushing dynprog value %d for hash %llu.\n", feasible, h->itemhash);
	dp_hashpush(h,feasible, tat);
    }

    h->items[item]--;
    dp_unhash(h,item);
    return feasible;
}

int maximum_feasible_dynprog(binconf *b, thread_attr *tat)
{
#ifdef MEASURE
    tat->maximum_feasible_counter++;
    auto start = std::chrono::system_clock::now(); 
#endif
    DEEP_DEBUG_PRINT("Starting dynprog maximization of configuration:\n");
    DEEP_DEBUG_PRINT_BINCONF(b);
    DEEP_DEBUG_PRINT("\n"); 

    // due to state-reversing memory copy should not be needed
    
    // calculate lower bound for the optimum using Best Fit Decreasing
    std::pair<int,int> fitresults = fitmaxone(b);
    
    int bestfitvalue = fitresults.second;
    int dynitem = bestfitvalue;
    
    DEEP_DEBUG_PRINT("lower bound for dynprog: %d\n", bestfitvalue);

    /* if(is_root(b)) {
	DEBUG_PRINT("ROOT: lower bound for dynprog: %d\n", bestfitvalue);
     } */
    // calculate upper bound for the optimum based on min(S,sum of remaining items)
    int maxvalue = (S*BINS) - totalload(b);
    if( maxvalue > S)
	maxvalue = S;

    DEEP_DEBUG_PRINT("upper bound for dynprog: %d\n", maxvalue);

    /* if(is_root(b)) {
	DEBUG_PRINT("ROOT: upper bound for dynprog: %d\n", maxvalue);
    }
    */

    // use binary search to find the value
    if (maxvalue > bestfitvalue)
    {
#ifdef MEASURE
	tat->until_break++;
#endif
	int lb = bestfitvalue; int ub = maxvalue;
	int mid = (lb+ub+1)/2;
	bool feasible;
	while (lb < ub)
	{
	    feasible = hash_and_test(b,mid,tat);
	    if (feasible)
	    {
		lb = mid;
	    } else {
		ub = mid-1;
	    }
	    
	    mid = (lb+ub+1)/2;
	}
	
	dynitem = lb;
    }

    /*
    assert(hash_and_test(b,dynitem,tat) == true);
    if(dynitem != maxvalue)
        assert(hash_and_test(b,dynitem+1,tat) == false);
    */ 
    
    
    // DEBUG: compare it with ordinary for cycle

    /*
    for (dynitem=maxvalue; dynitem>bestfitvalue; dynitem--)
    {
	bool feasible = hash_and_test(b,dynitem, tat);
	if (feasible)
	{
	    tat->until_break++;
	    break;
	}
    } */

#ifdef MEASURE
    auto end = std::chrono::system_clock::now();
    tat->dynprog_time += end - start;
#endif
    
    return dynitem;
}

#endif
