﻿using System;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.ServiceProcess;
using System.Threading;

namespace WinBtrfsService
{
	public partial class Service : ServiceBase
	{
		NamedPipeServerStream pipeServer;
		Thread threadPipeLoop;
		ManualResetEvent eventTerm, eventPipeConnection;
		
		public Service()
		{
			InitializeComponent();
		}

		protected override void OnStart(string[] args)
		{
			pipeServer = new NamedPipeServerStream("WinBtrfsService", PipeDirection.InOut, 1,
				PipeTransmissionMode.Message, PipeOptions.Asynchronous);

			eventTerm = new ManualResetEvent(false);
			eventPipeConnection = new ManualResetEvent(false);

			threadPipeLoop = new Thread(PipeLoop);
			threadPipeLoop.Start();

			Program.eventLog.WriteEntry("Service started.", EventLogEntryType.Information);
		}

		protected override void OnStop()
		{
			eventTerm.Set();
			
			Program.eventLog.WriteEntry("Service stopped.", EventLogEntryType.Information);
		}

		private void PipeLoop()
		{
			while (true)
			{
				bool wait = true;
				var result = pipeServer.BeginWaitForConnection(_ => eventPipeConnection.Set(), null);
				
				WaitHandle[] events = { eventTerm, eventPipeConnection };

				do
				{
					switch (WaitHandle.WaitAny(events))
					{
					case 0: // eventTerm
						return;
					case 1: // eventPipeConnection
						if (result.IsCompleted)
						{
							pipeServer.EndWaitForConnection(result);
							GotConnection();
							pipeServer.Disconnect();
							eventPipeConnection.Reset();
							wait = false;
						}
						break;
					case WaitHandle.WaitTimeout:
						break;
					}
				}
				while (wait);
			}
		}

		private void GotConnection()
		{
			byte[] buffer = new byte[102400];
			int bufferLen = 0;

			try
			{
				bufferLen = pipeServer.Read(buffer, 0, buffer.Length);
			}
			catch (ObjectDisposedException)
			{
				Program.eventLog.WriteEntry("The pipe closed unexpectedly when reading.",
					EventLogEntryType.Warning);
				return;
			}
			catch (InvalidOperationException)
			{
				Program.eventLog.WriteEntry("The pipe was in an unexpected state at read time.",
					EventLogEntryType.Warning);
				return;
			}
			catch (IOException e)
			{
				Program.eventLog.WriteEntry("Caught an IOException at read time: " + e.Message,
					EventLogEntryType.Warning);
				return;
			}

			if (pipeServer.IsMessageComplete)
			{
				String str = System.Text.Encoding.Unicode.GetString(buffer, 0, bufferLen);
				Program.eventLog.WriteEntry("Got a pipe connection. Message:\n" + str,
					EventLogEntryType.Information);
				string reply = HandleMessage(str);

				byte[] replyBytes = System.Text.Encoding.Unicode.GetBytes(reply.TrimEnd('\n'));

				try
				{
					pipeServer.Write(replyBytes, 0, replyBytes.Length);

					/* don't disconnect prematurely */
					pipeServer.WaitForPipeDrain();
				}
				catch (ObjectDisposedException)
				{
					Program.eventLog.WriteEntry("The pipe closed unexpectedly when writing.",
						EventLogEntryType.Warning);
					return;
				}
				catch (InvalidOperationException)
				{
					Program.eventLog.WriteEntry("The pipe was in an unexpected state at write time.",
						EventLogEntryType.Warning);
					return;
				}
				catch (IOException e)
				{
					Program.eventLog.WriteEntry("Caught an IOException at write time: " + e.Message,
						EventLogEntryType.Warning);
					return;
				}
			}
			else
				Program.eventLog.WriteEntry("A message larger than 100K arrived; discarding.",
					EventLogEntryType.Warning);
		}

		private string HandleMessage(string msg)
		{
			string[] lines = msg.Split('\n');
			string reply;

			if (lines[0] == "Mount")
			{
				Program.eventLog.WriteEntry("Received a Mount message.",
					EventLogEntryType.Information);

				reply = VolumeManager.Mount(lines);
			}
			else if (lines[0] == "List")
			{
				Program.eventLog.WriteEntry("Received a List message.",
					EventLogEntryType.Information);

				reply = "Data\n";

				if (VolumeManager.volumeTable.Count == 0)
					reply += "No Entries";
				else
				{
					foreach (var entry in VolumeManager.volumeTable)
					{
						reply += "Entry\n";
						reply += "fsUUID|" + entry.fsUUID.ToString() + "\n";
						reply += "mountData|optSubvol|" + entry.mountData.optSubvol.ToString() + "\n";
						reply += "mountData|optSubvolID|" + entry.mountData.optSubvolID.ToString() + "\n";
						reply += "mountData|optDump|" + entry.mountData.optDump.ToString() + "\n";
						reply += "mountData|optTestRun|" + entry.mountData.optTestRun.ToString() + "\n";
						reply += "mountData|mountPoint|" + entry.mountData.mountPoint.Length + "|" +
							entry.mountData.mountPoint + "\n";
						reply += "mountData|subvolName|" + entry.mountData.subvolName.Length + "|" +
							entry.mountData.subvolName + "\n";
						reply += "mountData|dumpFile|" + entry.mountData.dumpFile.Length + "|" +
							entry.mountData.dumpFile + "\n";
						reply += "mountData|subvolID|" + entry.mountData.subvolID.ToString() + "\n";

						foreach (string device in entry.mountData.devices)
							reply += "mountData|devices|" + device.Length + "|" + device + "\n";
					}
				}
			}
			else
			{
				Program.eventLog.WriteEntry("Received an unknown message: " + lines[0] + ".",
					EventLogEntryType.Warning);

				reply = "Error\nBad message type.";
			}

			return reply;
		}
	}
}
