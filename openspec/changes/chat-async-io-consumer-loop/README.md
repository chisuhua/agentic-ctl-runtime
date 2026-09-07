# chat-async-io-consumer-loop

Close consumer loop for chat-async-io queues (chat-async-io-queue-infra produced steering/follow_up queues but no consumer exists; main loop reads stdin in parallel with input thread → race + dead-producer). This change makes main loop consume queues instead of stdin (single-reader), so input thread can be unconditionally safe.
