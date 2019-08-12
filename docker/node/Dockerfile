FROM carlhua/logosbase:latest
ARG build

RUN apt-get update && \
      apt-get -y install sudo



USER root
CMD /bin/bash
ADD ./ /tmp/
RUN cd /tmp/build

RUN if [ "$build" = "true" ]; then \
    /tmp/build/build.sh; \
  fi

FROM carlhua/logosbase:latest

RUN apt-get -y install vim 

COPY --from=0 /tmp/build/logos_core /usr/bin

COPY docker/node/entry.sh /entry.sh
COPY docker/node/config /usr/share/logos/config
RUN chmod +x /entry.sh
CMD ["/bin/bash",  "/entry.sh"]