namespace dotnet
{
    public class MainThread : IDisposable
    {
        private Queue<Action> work = new Queue<Action>();
        private Thread workThread;
        private bool running = true;
        public MainThread()
        {
            workThread = new Thread(workConsumer);
            workThread.Start();
        }

        void workConsumer()
        {
            while(running)
            {
                lock(this)
                {
                    while(work.Count == 0 && running)
                    {
                        Monitor.Wait(this, 1000);
                    }
                    foreach (var job in work)
                    {
                        job();
                    }
                    work.Clear();
                }
            }
        }

        public void Dispose()
        {
            lock(this)
            {
                running = false;
            }
            workThread.Join();
        }

        public void Push(Action action)
        {
            lock(this)
            {
                work.Enqueue(action);
                Monitor.Pulse(this);
            }
        }
    }
}
