FROM oven/bun:1.4.5-alpine
WORKDIR /app
COPY package.json bun.lock ./
RUN bun install --production --frozen-lockfile
COPY server.js ./
COPY src src
COPY public public
ENV SIFT_DATA=/data PORT=8080
USER 1000:1000
CMD ["bun", "server.js"]
