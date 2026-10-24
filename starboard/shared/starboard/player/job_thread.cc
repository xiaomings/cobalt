// Copyright 2018 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "starboard/shared/starboard/player/job_thread.h"

#include <condition_variable>
#include <mutex>
#include <string>

#include "starboard/common/check_op.h"

namespace starboard {

class JobThread::WorkerThread : public Thread {
 public:
  // When |detached_job_queue| is null (original JobQueue implementation), the
  // JobQueue is created on the worker thread and handed over to the creating
  // thread via TakeJobQueue().  Otherwise the given detached JobQueue is bound
  // to the worker thread when it starts.
  WorkerThread(std::string_view thread_name,
               const ThreadOptions& options,
               JobQueue* detached_job_queue)
      : Thread(thread_name, options), detached_job_queue_(detached_job_queue) {}

  void Run() override {
    if (detached_job_queue_) {
      detached_job_queue_->BindToCurrentThread();
      detached_job_queue_->RunUntilStopped();
      return;
    }

    auto job_queue =
        std::make_unique<JobQueue>(/*use_optimized_implementation=*/false);
    JobQueue* job_queue_ptr = job_queue.get();
    {
      std::lock_guard lock(mutex_);
      job_queue_to_transfer_ = std::move(job_queue);
    }
    cv_.notify_one();
    job_queue_ptr->RunUntilStopped();
  }

  std::unique_ptr<JobQueue> TakeJobQueue() {
    SB_DCHECK(!detached_job_queue_);
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return job_queue_to_transfer_ != nullptr; });
    return std::move(job_queue_to_transfer_);
  }

 private:
  JobQueue* const detached_job_queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::unique_ptr<JobQueue> job_queue_to_transfer_;
};

std::unique_ptr<JobThread> JobThread::Create(std::string_view thread_name,
                                             const ThreadOptions& options,
                                             bool use_optimized_job_queue) {
  if (use_optimized_job_queue) {
    // The JobQueue is created detached from any thread and attached to the
    // worker thread once it runs, so creating a JobThread doesn't have to wait
    // for the new thread to be scheduled.  Jobs can be scheduled right away,
    // the worker thread picks them up when it starts.
    auto job_queue = std::make_unique<JobQueue>(
        JobQueue::kDetachedFromThread, /*use_optimized_implementation=*/true);
    auto thread =
        std::make_unique<WorkerThread>(thread_name, options, job_queue.get());
    thread->Start();
    return std::unique_ptr<JobThread>(
        new JobThread(std::move(thread), std::move(job_queue)));
  }

  auto thread = std::make_unique<WorkerThread>(thread_name, options,
                                               /*detached_job_queue=*/nullptr);
  thread->Start();
  auto job_queue = thread->TakeJobQueue();
  return std::unique_ptr<JobThread>(
      new JobThread(std::move(thread), std::move(job_queue)));
}

JobThread::~JobThread() {
  std::lock_guard lock(stop_mutex_);
  SB_CHECK(stopped_)
      << "JobThread::Stop() must be called before destruction (e.g. inside "
         "dtor body) to ensure any running task is finished and all pending "
         "tasks are cleared while the owner is still valid. See "
         "http://b/477902972#comment7";
}

void JobThread::Stop() {
  SB_CHECK(!job_queue_->BelongsToCurrentThread())
      << "Stop() should not be called from the worker thread itself. This "
         "would result in a deadlock during Join().";

  // Use a mutex to ensure that if multiple threads call Stop() (e.g. one
  // explicitly and one via the destructor), they all wait until the join
  // is actually complete.
  std::lock_guard lock(stop_mutex_);
  if (stopped_) {
    return;
  }
  stopped_ = true;

  job_queue_->StopSoon();
  thread_->Join();
}

JobThread::JobThread(std::unique_ptr<WorkerThread> thread,
                     std::unique_ptr<JobQueue> job_queue)
    : thread_(std::move(thread)), job_queue_(std::move(job_queue)) {}

}  // namespace starboard
