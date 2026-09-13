````sh
docker build --platform=linux/amd64 -f ubuntu-build-env.Dockerfile --tag saga-ubuntu-env .
```

```sh
docker run --rm -it --platform=linux/amd64 \
    --mount type=bind,src="$PWD",dst=/workspace \
    --mount type=volume,src=saga-bazel-cache,dst=/root/.cache/bazel \
    saga-ubuntu-env \
    bash
```

```sh
bazel build --config=target //src:saga_target && bazel run //scripts:generate_bazel_objdiff_report
```
