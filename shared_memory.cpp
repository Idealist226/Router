// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "include/shared_memory.h"
#include "include/log.h"

ShmPiece::ShmPiece(const char* name, int size)
{
	this->name = name;
	this->size = size;

	this->shm_fd = -1;
	this->ptr = NULL;
}

ShmPiece::~ShmPiece()
{
	LOG_INFO("~ShmPiece is running " << this->name);
	this->remove();
}

bool ShmPiece::open()
{
	/* open shared memory segment */
	this->shm_fd = shm_open(this->name.c_str(), O_CREAT | O_RDWR, 0666);
	
	/* set the size of shared memory segment */
	ftruncate(shm_fd, this->size);
	
	/* now map the shared memory segment in the address space of the process */
	this->ptr = mmap(0, this->size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, this->shm_fd, 0);
	
	if (this->ptr == MAP_FAILED){
		LOG_ERROR("Error mapping shared memory " << this->name);
		return false;
	}

	return true;
}

void ShmPiece::remove()
{
	if (this->ptr != MAP_FAILED)
	{
		if (munmap(this->ptr, this->size) == -1)
			LOG_ERROR("munmap: Error unmap " << this->ptr);
		if (close(this->shm_fd) == -1)
			LOG_ERROR("munmap: Error close " << this->shm_fd);
		if (shm_unlink(this->name.c_str()) == -1)
			LOG_ERROR("shm_unlink: Error removing " << this->name);
	}
}
