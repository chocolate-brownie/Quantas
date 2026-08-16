**Experiment Goal**

The goal is to understand whether the abstract simulation model can approximate the real BoostMQ execution model closely enough to be useful.

More specifically, we want to check whether different random-delay settings in the abstract model can produce results that match the BoostMQ backend within an acceptable margin, for example within 5%.

**Main Question**

Can we tune the abstract model’s delay parameters so that its behavior becomes comparable to the real message queue implementation?

**Planned Experiment**

Run the same experiments using both backends:

1. Abstract simulation backend.
2. BoostMQ/message queue backend.

For each example algorithm, run the same configuration under both settings and compare the results.

**Comparison Method**

Use the current configuration files as the starting point.

For the abstract backend, vary the average/random delay settings from `1` to `5`.

For the BoostMQ backend, measure real execution time.

Then compare the abstract results against the BoostMQ results to see which delay setting gives the closest match.

**Purpose**

The purpose is to get a practical sense of whether the abstract model is realistic.

If the abstract model can match BoostMQ results within roughly 5%, then it may be useful for faster experimentation.

If the results differ significantly, then the abstract backend may need better delay modeling, or we need to clearly document that it is only a logical simulator and not a realistic timing model.

**Refined Notes**

We want to run equivalent experiments in both the abstract backend and the BoostMQ backend. The objective is to determine whether the random delay parameters in the abstract model can be tuned so that the abstract execution time approximates the real BoostMQ execution time within about 5%.

Using the current configuration files, we should run all available examples with both backends, collect timing results, and compare them. For the abstract backend, we will vary the average delay from 1 to 5 and observe which setting most closely matches the measured real time of the BoostMQ backend.

The larger research question is whether the abstract simulation model is realistic enough for performance-oriented experiments, or whether it should only be used for logical correctness and algorithm behavior.


---

How we do we know the maximum number of messages waiting to be received properly so we can adjust the size using `sudo sysctl -w fs.mqueue.msg_max=N` if thats necessary
