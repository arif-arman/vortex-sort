/*------------------------------------------------------------------------------------------------
 -   Vortex: Extreme-Performance Memory Abstractions for Data-Intensive Streaming Applications   -
 -   Copyright(C) 2020 Carson Hanel, Arif Arman, Di Xiao, John Keech, Dmitri Loguinov            -
 -   Produced via research carried out by the Texas A&M Internet Research Lab                    -
 -																							     -
 -	 This program is free software : you can redistribute it and/or modify						 -
 -	 it under the terms of the GNU General Public License as published by						 -
 -	 the Free Software Foundation, either version 3 of the License, or							 -
 -	 (at your option) any later version.													     -
 -																								 -
 -	 This program is distributed in the hope that it will be useful,							 -
 -	 but WITHOUT ANY WARRANTY; without even the implied warranty of								 -
 -	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the								 -
 -	 GNU General Public License for more details.												 -
 -																								 -
 -	 You should have received a copy of the GNU General Public License							 -
 -	 along with this program. If not, see < http://www.gnu.org/licenses/>.						 -
 ------------------------------------------------------------------------------------------------*/
#include "stdafx.h"

// explicit template instantiation
template class Benchmark<uint64_t>;
template class Benchmark<uint32_t>;
template class Benchmark<uint16_t>;
template class Benchmark<uint8_t >;

// uniformly random linear congruential generator producer
template <typename ItemType>
__forceinline void WriterLCG(ItemType* p, uint64_t len, uint64_t& x, uint64_t& y, uint64_t& z, uint64_t& w) {
	// LCG additive from https://en.wikipedia.org/wiki/Linear_congruential_generator
	uint64_t a     = 6364136223846793005;
	uint64_t c     = 1442695040888963407;
	uint64_t upper = 64 - sizeof(ItemType) * 8;
	for (uint64_t i = 0; i < len; i += 4) {
		x        = x * a + c;
		y        = y * a + c;
		z        = z * a + c;
		w        = w * a + c;
 		p[i]     = (ItemType)(x >> upper);
		p[i + 1] = (ItemType)(y >> upper);
		p[i + 2] = (ItemType)(z >> upper);
		p[i + 3] = (ItemType)(w >> upper);
	}
}

// checks that the sort results in sorted data
template <typename ItemType>
void ConsumerChecker(ItemType* p, uint64_t len) {
	uint64_t failed = 0, prev = p[0];
	for (uint64_t j = 1; j < len; j++) {
		uint64_t cur = p[j];

		// check for out-of-order
		if (prev > cur) failed++;
		prev = cur;
	}
	printf("\tSorted Result: unsorted keys = %lld, processed keys = %lld\n", failed, len);
}


// --------------------------------------------------------------- //
//                     Vortex Benchmark Module                     // 
// --------------------------------------------------------------- //
template <typename ItemType>
void Benchmark<ItemType>::Run(int argc, char** argv) {
	// Vortex-Enabled In-Place MSD Radix Sort.
	
	uint64_t GB             = 1;
	uint64_t iterations     = 3;
	uint64_t itemsPerSort   = GB * (1LLU << 30) / sizeof(ItemType);
	uint64_t memory         = itemsPerSort * sizeof(ItemType);
	uint64_t blockSizePower = 20;
	Syscall.SetAffinity(0);

	printf("Running uniform %u GB random sort\n", GB);

	// setup the Vortex sort buckets
	VortexSort<ItemType>* vs = new VortexSort<ItemType>(itemsPerSort, blockSizePower);

	// prepare the input stream
	ItemType* inputBuf, *outputBuf;
	Stream*   inputS     = new VortexS(memory, memory, vs->sp, vs->nBuckets[0]);
	inputBuf = outputBuf = (ItemType*)inputS->GetReadBuf();

	for (uint64_t i = 0; i < iterations; i++) {
		// produce uniformly random data
		uint64_t x = (uint64_t)1e4, y = (uint64_t)1e12, z = (uint64_t)1e18, w = 3;
		WriterLCG<ItemType>((ItemType*)inputBuf, itemsPerSort, x, y, z, w);

		// run the sort
		void*  start = Syscall.StartTimer();
		vs->Sort(inputBuf, outputBuf, itemsPerSort);
		double elapsed = Syscall.EndTimer(start);

		// output the result
		double speed    = (double)itemsPerSort / elapsed / 1e6;
		double memUsed  = (double)(vs->sp->blockCount << blockSizePower);
		double memIdeal = (double)itemsPerSort * sizeof(ItemType);
		printf("\ttime %.3f sec, speed %.2f M/s, overhead %.2f%%, blocks %lld\n",
			elapsed, speed, (memUsed / memIdeal - 1) * 100, vs->sp->blockCount);

		// check sortedness
		ConsumerChecker(outputBuf, itemsPerSort);

		// reset the input buffer
		inputS->Reset();
	}
	delete inputS;
	delete vs;
}