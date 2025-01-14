# Troubleshooting

-   [Native method not found](#native-method-not-found)
-   [No registered calculator found](#no-registered-calculator-found)
-   [Out Of Memory error](#out-of-memory-error)
-   [Graph hangs](#graph-hangs)
-   [Calculator is scheduled infrequently](#calculator-is-scheduled-infrequently)
-   [Output timing is uneven](#output-timing-is-uneven)
-   [CalculatorGraph lags behind inputs](#calculatorgraph-lags-behind-inputs)

## Native method not found

The error message:

```
java.lang.UnsatisfiedLinkError: No implementation found for void com.google.wick.Wick.nativeWick
```

usually indicates that a needed native library, such as `/libwickjni.so` has not
been loaded or has not been included in the dependencies of the app or cannot be
found for some reason. Note that Java requires every native library to be
explicitly loaded using the function `System.loadLibrary`.

## No registered calculator found

The error message:

```
No registered object with name: OurNewCalculator; Unable to find Calculator "OurNewCalculator"
```

usually indicates that `OurNewCalculator` is referenced by name in a
[`CalculatorGraphConfig`] but that the library target for OurNewCalculator has
not been linked to the application binary. When a new calculator is added to a
calculator graph, that calculator must also be added as a build dependency of
the applications using the calculator graph.

This error is caught at runtime because calculator graphs reference their
calculators by name through the field `CalculatorGraphConfig::Node:calculator`.
When the library for a calculator is linked into an application binary, the
calculator is automatically registered by name through the
[`REGISTER_CALCULATOR`] macro using the [`registration.h`] library. Note that
[`REGISTER_CALCULATOR`] can register a calculator with a namespace prefix,
identical to its C++ namespace. In this case, the calcultor graph must also use
the same namespace prefix.

## Out Of Memory error

Exhausting memory can be a symptom of too many packets accumulating inside a
running MediaPipe graph. This can occur for a number of reasons, such as:

1.  Some calculators in the graph simply can't keep pace with the arrival of
    packets from a realtime input stream such as a video camera.
2.  Some calculators are waiting for packets that will never arrive.

For problem (1), it may be necessary to drop some old packets in older to
process the more recent packets. For some hints, see:
[How to process realtime input streams](how_to_questions.md#how-to-process-realtime-input-streams)

For problem (2), it could be that one input stream is lacking packets for some
reason. A device or a calculator may be misconfigured or may produce packets
only sporadically. This can cause downstream calculators to wait for many
packets that will never arrive, which in turn causes packets to accumulate on
some of their input streams. MediaPipe addresses this sort of problem using
"timestamp bounds". For some hints see:
[How to process realtime input streams](how_to_questions.md#how-to-process-realtime-input-streams)

The MediaPipe setting [`CalculatorGraphConfig::max_queue_size`] limits the
number of packets enqueued on any input stream by throttling inputs to the
graph. For realtime input streams, the number of packets queued at an input
stream should almost always be zero or one. If this is not the case, you may see
the following warning message:

```
Resolved a deadlock by increasing max_queue_size of input stream
```

Also, the setting [`CalculatorGraphConfig::report_deadlock`] can be set to cause
graph run to fail and surface the deadlock as an error, such that max_queue_size
to acts as a memory usage limit.

## Graph hangs

Many applications will call [`CalculatorGraph::CloseAllPacketSources`] and
[`CalculatorGraph::WaitUntilDone`] to finish or suspend execution of a MediaPipe
graph. The objective here is to allow any pending calculators or packets to
complete processing, and then to shutdown the graph. If all goes well, every
stream in the graph will reach [`Timestamp::Done`], and every calculator will
reach [`CalculatorBase::Close`], and then [`CalculatorGraph::WaitUntilDone`]
will complete successfully.

If some calculators or streams cannot reach state [`Timestamp::Done`] or
[`CalculatorBase::Close`], then the method [`CalculatorGraph::Cancel`] can be
called to terminate the graph run without waiting for all pending calculators
and packets to complete.

## Output timing is uneven

Some realtime MediaPipe graphs produce a series of video frames for viewing as a
video effect or as a video diagnostic. Sometimes, a MediaPipe graph will produce
these frames in clusters, for example when several output frames are
extrapolated from the same cluster of input frames. If the outputs are presented
as they are produced, some output frames are immediately replaced by later
frames in the same cluster, which makes the results hard to see and evaluate
visually. In cases like this, the output visualization can be improved by
presenting the frames at even intervals in real time.

MediaPipe addresses this use case by mapping timestamps to points in real time.
Each timestamp indicates a time in microseconds, and a calculator such as
`LiveClockSyncCalculator` can delay the output of packets to match their
timestamps. This sort of calculator adjusts the timing of outputs such that:

1.  The time between outputs corresponds to the time between timestamps as
    closely as possible.
2.  Outputs are produced with the smallest delay possible.

## CalculatorGraph lags behind inputs

For many realtime MediaPipe graphs, low latency is an objective. MediaPipe
supports "pipelined" style parallel processing in order to begin processing of
each packet as early as possible. Normally the lowest possible latency is the
total time required by each calculator along a "critical path" of successive
calculators. The latency of the a MediaPipe graph could be worse than the ideal
due to delays introduced to display frames a even intervals as described in
[Output timing is uneven](troubleshooting.md?cl=252235797#output-timing-is-uneven).

If some of the calculators in the graph cannot keep pace with the realtime input
streams, then latency will continue to increase, and it becomes necessary to
drop some input packets. The recommended technique is to use the MediaPipe
calculators designed specifically for this purpose such as
[`RealTimeFlowLimiterCalculator`] as described in
[How to process realtime input streams](how_to_questions.md#how-to-process-realtime-input-streams).

[`CalculatorGraphConfig`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/calculator.proto
[`CalculatorGraphConfig::max_queue_size`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/calculator.proto
[`CalculatorGraphConfig::report_deadlock`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/calculator.proto
[`REGISTER_CALCULATOR`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/calculator_registry.h
[`registration.h`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/deps/registration.h
[`CalculatorGraph::CloseAllPacketSources`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/calculator_graph.h
[`CalculatorGraph::Cancel`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/calculator_graph.h
[`CalculatorGraph::WaitUntilDone`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/calculator_graph.h
[`Timestamp::Done`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/timestamp.h
[`CalculatorBase::Close`]: https://github.com/google/mediapipe/tree/master/mediapipe/framework/calculator_base.h
[`RealTimeFlowLimiterCalculator`]: https://github.com/google/mediapipe/tree/master/mediapipe/calculators/core/real_time_flow_limiter_calculator.cc
