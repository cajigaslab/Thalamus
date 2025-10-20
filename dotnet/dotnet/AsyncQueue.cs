namespace Thalamus
{
    public class AsyncQueue<T> : IDisposable, IAsyncEnumerable<T>
    {
        private Queue<T> queue = [];
        private readonly List<TaskCompletionSource> pending = [];

        public void Dispose()
        {
            lock (queue)
            {
                foreach (var task in pending)
                {
                    task.SetCanceled();
                }
                pending.Clear();
            }
        }

        public async Task<T> Get()
        {
            while (true)
            {
                var pendingTask = new TaskCompletionSource();
                lock (queue)
                {
                    if (queue.Count == 0)
                    {
                        pending.Add(pendingTask);
                    }
                    else
                    {
                        return queue.Dequeue();
                    }
                }
                await pendingTask.Task;
            }
        }

        public void Put(T item)
        {
            lock (queue)
            {
                queue.Enqueue(item);
                foreach (var task in pending)
                {
                    task.SetResult();
                }
                pending.Clear();
            }
        }

        public class Enumerator(AsyncQueue<T> queue) : IAsyncEnumerator<T>
        {
            private readonly AsyncQueue<T> queue = queue;

            private T? _current = default;
            public T Current
            {
                get
                {
                    if (_current == null)
                    {
                        throw new InvalidOperationException();
                    }
                    return _current;
                }
            }

            public ValueTask DisposeAsync()
            {
                return ValueTask.CompletedTask;
            }

            public async ValueTask<bool> MoveNextAsync()
            {
                try
                {
                    _current = await queue.Get();
                    return true;
                }
                catch (OperationCanceledException ex)
                {
                    return false;
                }
            }
        }

        public IAsyncEnumerator<T> GetAsyncEnumerator(CancellationToken cancellationToken = default)
        {
            return new AsyncQueue<T>.Enumerator(this);
        }
    }
}
