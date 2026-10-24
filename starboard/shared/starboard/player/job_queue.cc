// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/player/job_queue.h"

#include <errno.h>
#include <pthread.h>
#include <time.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "starboard/common/time.h"
#include "starboard/shared/starboard/thread_checker.h"
#include "starboard/system.h"

namespace starboard {

namespace {

constexpr int64_t kMicrosecondsPerSecond = 1'000'000;

#if ENABLE_JOB_QUEUE_PROFILING
// Reset the max value periodically to catch all local peaks.
constexpr int64_t kProfileResetIntervalUsec = 1'000'000;  // 1 second
constexpr int kProfileStackDepth = 10;
#endif  // ENABLE_JOB_QUEUE_PROFILING

timespec ToTimespec(int64_t usec) {
  timespec ts;
  const int64_t seconds = usec / kMicrosecondsPerSecond;
  if (seconds >= static_cast<int64_t>(std::numeric_limits<time_t>::max())) {
    ts.tv_sec = std::numeric_limits<time_t>::max();
    ts.tv_nsec = 0;
    return ts;
  }
  ts.tv_sec = static_cast<time_t>(seconds);
  ts.tv_nsec = static_cast<long>((usec % kMicrosecondsPerSecond) * 1000);
  return ts;
}

}  // namespace

const JobQueue::JobToken JobQueue::JobToken::kUnscheduled =
    JobQueue::JobToken();

JobQueue::JobToken JobQueue::JobToken::Generate(int64_t scheduled_time,
                                                bool is_delayed) {
  static std::atomic<int64_t> s_current_token{kInvalidToken};
  return JobToken(s_current_token.fetch_add(1, std::memory_order_relaxed) + 1,
                  scheduled_time, is_delayed);
}

// The interface shared by the two implementations.  The JobQueue class itself
// only forwards to it.
class JobQueue::Impl {
 public:
  virtual ~Impl() = default;

  virtual void BindToCurrentThread() = 0;
  virtual bool BelongsToCurrentThread() const = 0;
  virtual JobToken Schedule(Job job, JobOwner* owner, int64_t delay_usec) = 0;
  virtual void ScheduleAndWait(Job job) = 0;
  virtual void RemoveJobByToken(JobToken* job_token) = 0;
  virtual void RemoveJobsByOwner(JobOwner* owner) = 0;
  virtual void StopSoon() = 0;
  virtual void RunUntilStopped() = 0;
  virtual void RunUntilIdle() = 0;
};

// ============================================================================
// The original implementation, kept as is.
// ============================================================================
class JobQueue::LegacyImpl final : public JobQueue::Impl {
 public:
  LegacyImpl() = default;
  ~LegacyImpl() override { StopSoon(); }

  void BindToCurrentThread() override {
    SB_CHECK(thread_checker_.CalledOnValidThread())
        << "The original JobQueue implementation is bound to the thread "
           "creating it.";
  }

  bool BelongsToCurrentThread() const override {
    return thread_checker_.CalledOnValidThread();
  }

  JobToken Schedule(Job job, JobOwner* owner, int64_t delay_usec) override {
    SB_DCHECK(job);
    SB_DCHECK_GE(delay_usec, 0);

    std::lock_guard lock(mutex_);
    if (stopped_) {
      return JobToken::kUnscheduled;
    }

    JobToken job_token = JobToken::Generate();
    JobRecord job_record = {job_token, std::move(job), owner};
#if ENABLE_JOB_QUEUE_PROFILING
    if (kProfileStackDepth > 0) {
      job_record.stack_size =
          SbSystemGetStack(job_record.stack, kProfileStackDepth);
    }
#endif  // ENABLE_JOB_QUEUE_PROFILING

    int64_t time_to_run_job = CurrentMonotonicTime() + delay_usec;
    bool is_first_job =
        time_to_job_record_map_.empty() ||
        time_to_run_job < time_to_job_record_map_.begin()->first;

    time_to_job_record_map_.insert({time_to_run_job, std::move(job_record)});
    if (is_first_job) {
      condition_.notify_all();
    }
    return job_token;
  }

  void ScheduleAndWait(Job job) override {
    // TODO: Allow calling from the JobQueue thread.
    SB_CHECK(!BelongsToCurrentThread());

    Schedule(std::move(job), /*owner=*/nullptr, /*delay_usec=*/0);

    bool job_finished = false;

    Schedule(
        [&]() {
          {
            std::lock_guard lock(mutex_);
            job_finished = true;
          }
          condition_.notify_all();
        },
        /*owner=*/nullptr, /*delay_usec=*/0);

    std::unique_lock lock(mutex_);
    condition_.wait(lock, [&] { return job_finished || stopped_; });
  }

  void RemoveJobByToken(JobToken* job_token) override {
    SB_CHECK(BelongsToCurrentThread());

    if (!*job_token) {
      return;
    }

    {
      std::lock_guard lock(mutex_);
      for (TimeToJobRecordMap::iterator iter = time_to_job_record_map_.begin();
           iter != time_to_job_record_map_.end(); ++iter) {
        if (iter->second.job_token == *job_token) {
          time_to_job_record_map_.erase(iter);
          break;
        }
      }
    }
    job_token->Reset();
  }

  void RemoveJobsByOwner(JobOwner* owner) override {
    SB_CHECK(BelongsToCurrentThread());
    SB_DCHECK(owner);

    std::lock_guard lock(mutex_);
    for (TimeToJobRecordMap::iterator iter = time_to_job_record_map_.begin();
         iter != time_to_job_record_map_.end();) {
      if (iter->second.owner == owner) {
        iter = time_to_job_record_map_.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  void StopSoon() override {
    {
      std::lock_guard lock(mutex_);
      stopped_ = true;
      time_to_job_record_map_.clear();
    }
    condition_.notify_all();
  }

  void RunUntilStopped() override {
    SB_CHECK(BelongsToCurrentThread());

    for (;;) {
      {
        std::unique_lock lock(mutex_);
        if (stopped_) {
          return;
        }
      }
      TryToRunOneJob(/*wait_for_next_job =*/true);
    }
  }

  void RunUntilIdle() override {
    SB_CHECK(BelongsToCurrentThread());

    while (TryToRunOneJob(/*wait_for_next_job =*/false)) {
    }
  }

 private:
  struct JobRecord {
    JobToken job_token;
    Job job;
    JobOwner* owner = nullptr;
#if ENABLE_JOB_QUEUE_PROFILING
    void* stack[kProfileStackDepth];
    int stack_size;
#endif  // ENABLE_JOB_QUEUE_PROFILING
  };
  typedef std::multimap<int64_t, JobRecord> TimeToJobRecordMap;

  // Return true if a job is run, otherwise return false.  When there is no job
  // ready to run currently and |wait_for_next_job| is true, the function will
  // wait to until a job is available or if the |queue_| is woken up.  Note that
  // set |wait_for_next_job| to true doesn't guarantee that one job will always
  // be run.
  bool TryToRunOneJob(bool wait_for_next_job) {
    SB_CHECK(BelongsToCurrentThread());

    JobRecord job_record;

    {
      std::unique_lock lock(mutex_);
      if (stopped_) {
        return false;
      }
      if (time_to_job_record_map_.empty() && wait_for_next_job) {
        condition_.wait(lock, [this] {
          return stopped_ || !time_to_job_record_map_.empty();
        });
#if ENABLE_JOB_QUEUE_PROFILING
        ++wait_times_;
#endif  // ENABLE_JOB_QUEUE_PROFILING
      }
      if (time_to_job_record_map_.empty()) {
        return false;
      }
      TimeToJobRecordMap::iterator first_delayed_job =
          time_to_job_record_map_.begin();
      int64_t delay_usec = first_delayed_job->first - CurrentMonotonicTime();
      if (delay_usec > 0) {
        if (wait_for_next_job) {
          int64_t scheduled_time = first_delayed_job->first;
          condition_.wait_for(
              lock, std::chrono::microseconds(delay_usec),
              [this, scheduled_time] {
                return stopped_ || time_to_job_record_map_.empty() ||
                       time_to_job_record_map_.begin()->first != scheduled_time;
              });
#if ENABLE_JOB_QUEUE_PROFILING
          ++wait_times_;
#endif  // ENABLE_JOB_QUEUE_PROFILING
          if (time_to_job_record_map_.empty()) {
            return false;
          }
        } else {
          return false;
        }
      }
      // Try to retrieve the job again as the job map can be altered during the
      // wait.
      first_delayed_job = time_to_job_record_map_.begin();
      delay_usec = first_delayed_job->first - CurrentMonotonicTime();
      if (delay_usec > 0) {
        return false;
      }
      job_record = std::move(first_delayed_job->second);
      time_to_job_record_map_.erase(first_delayed_job);
    }

    SB_DCHECK(job_record.job);

#if ENABLE_JOB_QUEUE_PROFILING
    int64_t start = CurrentMonotonicTime();
#endif  // ENABLE_JOB_QUEUE_PROFILING

    job_record.job();

#if ENABLE_JOB_QUEUE_PROFILING
    ++jobs_processed_;

    auto now = CurrentMonotonicTime();
    auto elapsed = now - start;
    if (elapsed > max_job_interval_) {
      job_record_with_max_interval_ = std::move(job_record);
      max_job_interval_ = elapsed;
    }
    if (now - last_reset_time_ > kProfileResetIntervalUsec) {
      SB_LOG(INFO) << "================ " << jobs_processed_
                   << " jobs processed, and waited for " << wait_times_
                   << " times since last reset on 0x" << this
                   << ", max job takes " << max_job_interval_;
      for (int i = 0; i < job_record_with_max_interval_.stack_size; ++i) {
        char function_name[1024];
        // Subtract one as return address of function may be in the next
        // function when a function is annotated as noreturn.
        const void* call =
            static_cast<const char*>(job_record_with_max_interval_.stack[i]) -
            1;
        if (SbSystemSymbolize(call, function_name,
                              SB_ARRAY_SIZE_INT(function_name))) {
          SB_LOG(INFO) << "    " << function_name;
        } else {
          SB_LOG(INFO) << "    " << job_record_with_max_interval_.stack[i];
        }
      }
      last_reset_time_ = now;
      max_job_interval_ = 0;
      jobs_processed_ = 0;
      wait_times_ = 0;
    }
#endif  // ENABLE_JOB_QUEUE_PROFILING
    return true;
  }

  ThreadChecker thread_checker_;
  std::mutex mutex_;
  std::condition_variable condition_;
  TimeToJobRecordMap time_to_job_record_map_;  // Guarded by |mutex_|.
  bool stopped_ = false;

#if ENABLE_JOB_QUEUE_PROFILING
  int64_t last_reset_time_ = CurrentMonotonicTime();
  JobRecord job_record_with_max_interval_;
  int64_t max_job_interval_ = 0;
  int jobs_processed_ = 0;
  int wait_times_ = 0;
#endif  // ENABLE_JOB_QUEUE_PROFILING
};

// ============================================================================
// The optimized implementation.
// ============================================================================
class JobQueue::OptimizedImpl final : public JobQueue::Impl {
 public:
  explicit OptimizedImpl(bool bind_to_current_thread) {
    if (bind_to_current_thread) {
      BindToCurrentThread();
    }
  }
  ~OptimizedImpl() override { StopSoon(); }

  void BindToCurrentThread() override {
    const pid_t thread_id = ThreadChecker::GetThreadId();
    SB_CHECK(thread_id != 0);
    const pid_t previous_thread_id =
        thread_id_.exchange(thread_id, std::memory_order_relaxed);
    SB_CHECK(previous_thread_id == 0 || previous_thread_id == thread_id)
        << "JobQueue is already attached to thread " << previous_thread_id;
  }

  bool BelongsToCurrentThread() const override {
    // A relaxed load is enough: the only non-zero value ever stored is the id
    // of the thread running the jobs.  On that thread the store happened
    // before in program order, and on any other thread the comparison fails
    // whether the store is visible or not.
    return thread_id_.load(std::memory_order_relaxed) ==
           ThreadChecker::GetThreadId();
  }

  JobToken Schedule(Job job, JobOwner* owner, int64_t delay_usec) override {
    SB_DCHECK(job);
    SB_DCHECK_GE(delay_usec, 0);

    JobToken job_token;
    bool wake_worker = false;
    {
      std::lock_guard lock(mutex_);
      if (stopped_) {
        return JobToken::kUnscheduled;
      }

      // The time and the token are both taken under the lock, so that the
      // order of the tokens is consistent with the order of the scheduled
      // times.
      const int64_t now = CurrentMonotonicTime();
      const bool is_delayed = delay_usec > 0;
      const int64_t time_to_run_job = now + delay_usec;
      job_token = JobToken::Generate(time_to_run_job, is_delayed);

      JobRecord job_record = {job_token, std::move(job), owner};
#if ENABLE_JOB_QUEUE_PROFILING
      if (kProfileStackDepth > 0) {
        job_record.stack_size =
            SbSystemGetStack(job_record.stack, kProfileStackDepth);
      }
#endif  // ENABLE_JOB_QUEUE_PROFILING

      // The worker only waits when there is no job to run, in which case any
      // job without a delay has to wake it up, and a delayed job only has to
      // wake it up when it becomes the earliest pending job (otherwise the
      // current wait of the worker already ends at the right time).
      if (is_delayed) {
        wake_worker =
            worker_waiting_ && (delayed_jobs_.empty() ||
                                time_to_run_job < delayed_jobs_.begin()->first);
        delayed_jobs_.emplace(time_to_run_job, std::move(job_record));
      } else {
        wake_worker = worker_waiting_;
        immediate_jobs_.push_back(std::move(job_record));
      }
    }
    // Signal outside of the lock, so the woken up worker doesn't immediately
    // block on the mutex still held by this thread.
    if (wake_worker) {
      worker_condition_.Signal();
    }
    return job_token;
  }

  void ScheduleAndWait(Job job) override {
    // TODO: Allow calling from the JobQueue thread.
    SB_CHECK(!BelongsToCurrentThread());

    // The state is shared with the job instead of being kept on the stack, so
    // the job can never access a destroyed object, even when this function
    // returns early because the queue is stopped while the job is running.
    struct SyncState {
      bool job_finished = false;
    };
    auto state = std::make_shared<SyncState>();

    Schedule(
        [this, state, job = std::move(job)]() mutable {
          job();
          {
            std::lock_guard lock(mutex_);
            state->job_finished = true;
          }
          sync_condition_.notify_all();
        },
        /*owner=*/nullptr, /*delay_usec=*/0);

    std::unique_lock lock(mutex_);
    sync_condition_.wait(lock, [&] { return state->job_finished || stopped_; });
  }

  void RemoveJobByToken(JobToken* job_token) override {
    SB_CHECK(BelongsToCurrentThread());

    if (!*job_token) {
      return;
    }

    // The job is destroyed outside of the lock, as its destructor may release
    // arbitrary resources (e.g. media buffers).
    std::optional<JobRecord> removed_job_record;
    {
      std::lock_guard lock(mutex_);
      if (job_token->is_delayed_) {
        auto range = delayed_jobs_.equal_range(job_token->scheduled_time_);
        for (auto iter = range.first; iter != range.second; ++iter) {
          if (iter->second.job_token == *job_token) {
            removed_job_record.emplace(std::move(iter->second));
            delayed_jobs_.erase(iter);
            break;
          }
        }
      } else {
        // |immediate_jobs_| is sorted by token.
        auto iter = std::lower_bound(
            immediate_jobs_.begin(), immediate_jobs_.end(), job_token->token_,
            [](const JobRecord& job_record, int64_t token) {
              return job_record.job_token.token_ < token;
            });
        if (iter != immediate_jobs_.end() && iter->job_token == *job_token) {
          removed_job_record.emplace(std::move(*iter));
          immediate_jobs_.erase(iter);
        }
      }
    }
    job_token->Reset();
  }

  void RemoveJobsByOwner(JobOwner* owner) override {
    SB_CHECK(BelongsToCurrentThread());
    SB_DCHECK(owner);

    // The jobs are destroyed outside of the lock, as their destructors may
    // release arbitrary resources (e.g. media buffers).
    std::vector<JobRecord> removed_job_records;
    {
      std::lock_guard lock(mutex_);
      auto new_end =
          std::remove_if(immediate_jobs_.begin(), immediate_jobs_.end(),
                         [&](JobRecord& job_record) {
                           if (job_record.owner != owner) {
                             return false;
                           }
                           removed_job_records.push_back(std::move(job_record));
                           return true;
                         });
      immediate_jobs_.erase(new_end, immediate_jobs_.end());

      for (auto iter = delayed_jobs_.begin(); iter != delayed_jobs_.end();) {
        if (iter->second.owner == owner) {
          removed_job_records.push_back(std::move(iter->second));
          iter = delayed_jobs_.erase(iter);
        } else {
          ++iter;
        }
      }
    }
  }

  void StopSoon() override {
    // The pending jobs are destroyed outside of the lock, as their destructors
    // may release arbitrary resources (e.g. media buffers).
    std::deque<JobRecord> immediate_jobs;
    TimeToJobRecordMap delayed_jobs;
    {
      std::lock_guard lock(mutex_);
      stopped_ = true;
      immediate_jobs.swap(immediate_jobs_);
      delayed_jobs.swap(delayed_jobs_);
    }
    worker_condition_.Signal();
    sync_condition_.notify_all();
  }

  void RunUntilStopped() override {
    SB_CHECK(BelongsToCurrentThread());

    while (!stopped_.load(std::memory_order_relaxed)) {
      TryToRunOneJob(/*wait_for_next_job =*/true);
    }
  }

  void RunUntilIdle() override {
    SB_CHECK(BelongsToCurrentThread());

    while (TryToRunOneJob(/*wait_for_next_job =*/false)) {
    }
  }

 private:
  // A condition variable whose timed waits are based on CLOCK_MONOTONIC, the
  // clock used by CurrentMonotonicTime() to schedule the jobs:
  // - The deadline can be passed as is, without any relative time conversions
  //   that accumulate drift and lead to extra wake ups.
  // - Changes of the wall clock (e.g. NTP updates at boot) don't affect the
  //   waits.  std::condition_variable cannot guarantee this: with Android API
  //   levels before 30 and glibc before 2.30 it converts all timed waits to
  //   CLOCK_REALTIME, and needs two extra clock reads for every wait to do so.
  class MonotonicConditionVariable {
   public:
    MonotonicConditionVariable() {
      pthread_condattr_t attributes;
      SB_CHECK_EQ(pthread_condattr_init(&attributes), 0);
#if !defined(__APPLE__)
      SB_CHECK_EQ(pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC), 0);
#endif  // !defined(__APPLE__)
      SB_CHECK_EQ(pthread_cond_init(&condition_, &attributes), 0);
      pthread_condattr_destroy(&attributes);
    }
    ~MonotonicConditionVariable() { pthread_cond_destroy(&condition_); }

    MonotonicConditionVariable(const MonotonicConditionVariable&) = delete;
    MonotonicConditionVariable& operator=(const MonotonicConditionVariable&) =
        delete;

    void Wait(std::unique_lock<std::mutex>& lock) {
      SB_DCHECK(lock.owns_lock());
      pthread_cond_wait(&condition_, lock.mutex()->native_handle());
    }

    // Waits until |deadline_usec|, which is a CurrentMonotonicTime() based
    // timestamp, or until Signal() is called.
    void WaitUntil(std::unique_lock<std::mutex>& lock, int64_t deadline_usec) {
      SB_DCHECK(lock.owns_lock());
#if defined(__APPLE__)
      // Apple platforms don't support pthread_condattr_setclock(), but offer a
      // relative wait that is not affected by wall clock changes.
      const int64_t delay_usec = deadline_usec - CurrentMonotonicTime();
      if (delay_usec <= 0) {
        return;
      }
      const timespec ts = ToTimespec(delay_usec);
      pthread_cond_timedwait_relative_np(&condition_,
                                         lock.mutex()->native_handle(), &ts);
#else   // defined(__APPLE__)
      const timespec ts = ToTimespec(deadline_usec);
      pthread_cond_timedwait(&condition_, lock.mutex()->native_handle(), &ts);
#endif  // defined(__APPLE__)
    }

    void Signal() { pthread_cond_signal(&condition_); }

   private:
    pthread_cond_t condition_;
  };

  struct JobRecord {
    JobToken job_token;
    Job job;
    JobOwner* owner = nullptr;
#if ENABLE_JOB_QUEUE_PROFILING
    void* stack[kProfileStackDepth];
    int stack_size;
#endif  // ENABLE_JOB_QUEUE_PROFILING
  };
  typedef std::multimap<int64_t, JobRecord> TimeToJobRecordMap;

  // Waits on |worker_condition_| until |deadline_usec| when it is set, or
  // until the worker is signaled.  |mutex_| has to be held.
  void WaitForJobs(std::unique_lock<std::mutex>& lock,
                   std::optional<int64_t> deadline_usec) {
    SB_DCHECK(lock.owns_lock());
    SB_DCHECK(!worker_waiting_);

    worker_waiting_ = true;
    if (deadline_usec) {
      worker_condition_.WaitUntil(lock, *deadline_usec);
    } else {
      worker_condition_.Wait(lock);
    }
    worker_waiting_ = false;

#if ENABLE_JOB_QUEUE_PROFILING
    ++wait_times_;
#endif  // ENABLE_JOB_QUEUE_PROFILING
  }

  // Return true if a job is run, otherwise return false.  When there is no job
  // ready to run currently and |wait_for_next_job| is true, the function will
  // wait until a job is available or until the queue is stopped.
  bool TryToRunOneJob(bool wait_for_next_job) {
    SB_CHECK(BelongsToCurrentThread());

    JobRecord job_record;

    {
      std::unique_lock lock(mutex_);
      for (;;) {
        if (stopped_) {
          return false;
        }

        JobRecord* immediate_job =
            immediate_jobs_.empty() ? nullptr : &immediate_jobs_.front();
        auto delayed_job = delayed_jobs_.begin();
        const bool has_delayed_job = delayed_job != delayed_jobs_.end();

        if (!immediate_job && !has_delayed_job) {
          if (!wait_for_next_job) {
            return false;
          }
          WaitForJobs(lock, std::nullopt);
          continue;
        }

        // Run the job with the earliest (scheduled time, token).  Jobs without
        // a delay are scheduled at the time Schedule() is called, so they are
        // always ready to run, and when a delayed job is picked over one of
        // them it is also ready to run.  Only when there is no job without a
        // delay the picked delayed job may have to wait.
        bool run_delayed_job = false;
        if (has_delayed_job) {
          if (!immediate_job) {
            run_delayed_job = true;
            const int64_t delay_usec =
                delayed_job->first - CurrentMonotonicTime();
            if (delay_usec > 0) {
              if (!wait_for_next_job) {
                return false;
              }
              WaitForJobs(lock, delayed_job->first);
              continue;
            }
          } else {
            const int64_t immediate_job_time =
                immediate_job->job_token.scheduled_time_;
            run_delayed_job = delayed_job->first < immediate_job_time ||
                              (delayed_job->first == immediate_job_time &&
                               delayed_job->second.job_token.token_ <
                                   immediate_job->job_token.token_);
          }
        }

        if (run_delayed_job) {
          job_record = std::move(delayed_job->second);
          delayed_jobs_.erase(delayed_job);
        } else {
          job_record = std::move(*immediate_job);
          immediate_jobs_.pop_front();
        }
        break;
      }
    }

    SB_DCHECK(job_record.job);

#if ENABLE_JOB_QUEUE_PROFILING
    int64_t start = CurrentMonotonicTime();
#endif  // ENABLE_JOB_QUEUE_PROFILING

    job_record.job();

#if ENABLE_JOB_QUEUE_PROFILING
    ++jobs_processed_;

    auto now = CurrentMonotonicTime();
    auto elapsed = now - start;
    if (elapsed > max_job_interval_) {
      job_record_with_max_interval_ = std::move(job_record);
      max_job_interval_ = elapsed;
    }
    if (now - last_reset_time_ > kProfileResetIntervalUsec) {
      SB_LOG(INFO) << "================ " << jobs_processed_
                   << " jobs processed, and waited for " << wait_times_
                   << " times since last reset on 0x" << this
                   << ", max job takes " << max_job_interval_;
      for (int i = 0; i < job_record_with_max_interval_.stack_size; ++i) {
        char function_name[1024];
        // Subtract one as return address of function may be in the next
        // function when a function is annotated as noreturn.
        const void* call =
            static_cast<const char*>(job_record_with_max_interval_.stack[i]) -
            1;
        if (SbSystemSymbolize(call, function_name,
                              SB_ARRAY_SIZE_INT(function_name))) {
          SB_LOG(INFO) << "    " << function_name;
        } else {
          SB_LOG(INFO) << "    " << job_record_with_max_interval_.stack[i];
        }
      }
      last_reset_time_ = now;
      max_job_interval_ = 0;
      jobs_processed_ = 0;
      wait_times_ = 0;
    }
#endif  // ENABLE_JOB_QUEUE_PROFILING
    return true;
  }

  // The id of the thread running the jobs, 0 when the JobQueue isn't attached
  // to a thread yet.  Written once by BindToCurrentThread(), read on any
  // thread by BelongsToCurrentThread().
  std::atomic<pid_t> thread_id_{0};
  std::mutex mutex_;
  // Signaled when the thread running the jobs has to wake up, i.e. when a job
  // that runs before all pending jobs is scheduled, or when the queue is
  // stopped.  The thread running the jobs is the only waiter.
  MonotonicConditionVariable worker_condition_;
  // Signaled when a job scheduled via ScheduleAndWait() is finished, or when
  // the queue is stopped.
  std::condition_variable sync_condition_;

  // Jobs scheduled without a delay, in the order they were scheduled, which is
  // also the order of their scheduled times and tokens.  Guarded by |mutex_|.
  //
  // Keeping the jobs without a delay (the vast majority) separated from the
  // delayed jobs makes scheduling and dispatching them O(1), and avoids a heap
  // allocation per job.
  std::deque<JobRecord> immediate_jobs_;
  // Jobs scheduled with a delay, keyed by their scheduled times.  Guarded by
  // |mutex_|.
  TimeToJobRecordMap delayed_jobs_;
  // True while the thread running the jobs is waiting in WaitForJobs().  Used
  // to avoid signaling |worker_condition_| (a syscall) when the worker isn't
  // waiting.  Guarded by |mutex_|.
  bool worker_waiting_ = false;
  // Written with |mutex_| held, but can be read without the lock.
  std::atomic<bool> stopped_{false};

#if ENABLE_JOB_QUEUE_PROFILING
  int64_t last_reset_time_ = CurrentMonotonicTime();
  JobRecord job_record_with_max_interval_;
  int64_t max_job_interval_ = 0;
  int jobs_processed_ = 0;
  int wait_times_ = 0;
#endif  // ENABLE_JOB_QUEUE_PROFILING
};

// ============================================================================
// JobQueue
// ============================================================================
JobQueue::JobQueue(bool use_optimized_implementation)
    : uses_optimized_implementation_(use_optimized_implementation),
      impl_(use_optimized_implementation
                ? static_cast<Impl*>(
                      new OptimizedImpl(/*bind_to_current_thread=*/true))
                : static_cast<Impl*>(new LegacyImpl())) {}

JobQueue::JobQueue(DetachedFromThread, bool use_optimized_implementation)
    : uses_optimized_implementation_(use_optimized_implementation),
      impl_(new OptimizedImpl(/*bind_to_current_thread=*/false)) {
  SB_CHECK(use_optimized_implementation)
      << "Only the optimized JobQueue implementation can be created detached "
         "from a thread.";
}

JobQueue::~JobQueue() = default;

void JobQueue::BindToCurrentThread() {
  impl_->BindToCurrentThread();
}

JobQueue::JobToken JobQueue::Schedule(Job job, int64_t delay_usec /*= 0*/) {
  return impl_->Schedule(std::move(job), /*owner=*/nullptr, delay_usec);
}

void JobQueue::ScheduleAndWait(Job job) {
  impl_->ScheduleAndWait(std::move(job));
}

void JobQueue::RemoveJobByToken(JobToken* job_token) {
  impl_->RemoveJobByToken(job_token);
}

void JobQueue::StopSoon() {
  impl_->StopSoon();
}

void JobQueue::RunUntilStopped() {
  impl_->RunUntilStopped();
}

void JobQueue::RunUntilIdle() {
  impl_->RunUntilIdle();
}

bool JobQueue::BelongsToCurrentThread() const {
  return impl_->BelongsToCurrentThread();
}

JobQueue::JobToken JobQueue::Schedule(Job job,
                                      JobOwner* owner,
                                      int64_t delay_usec) {
  return impl_->Schedule(std::move(job), owner, delay_usec);
}

void JobQueue::RemoveJobsByOwner(JobOwner* owner) {
  impl_->RemoveJobsByOwner(owner);
}

}  // namespace starboard
