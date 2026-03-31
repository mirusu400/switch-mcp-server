FROM devkitpro/devkita64:latest

WORKDIR /src

COPY . .

RUN make
