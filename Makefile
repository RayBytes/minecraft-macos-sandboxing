.PHONY: all clean test agent broker

all: entry broker agent

entry: build/mcguard-entry

broker: build/mcguard-broker

agent: build/mcguard-agent.jar

build/mcguard-broker: broker/mcguard-broker.c
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror $< -o $@ -lcurl
	@codesign --force --sign - --options runtime $@

build/mcguard-entry: broker/mcguard-entry.c
	@mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror $< -o $@
	@codesign --force --sign - --options runtime $@

AGENT_SOURCES := $(shell find mcguard-agent/src/main/java -name '*.java' 2>/dev/null)
ASM_JAR := $(shell find "$(HOME)/Library/Application Support/PrismLauncher/libraries/org/ow2/asm/asm" -type f -name 'asm-*.jar' 2>/dev/null | sort -V | tail -1)
ASM_COMMONS_JAR := $(shell find "$(HOME)/Library/Application Support/PrismLauncher/libraries/org/ow2/asm/asm-commons" -type f -name 'asm-commons-*.jar' 2>/dev/null | sort -V | tail -1)

build/mcguard-agent.jar: $(AGENT_SOURCES) mcguard-agent/MANIFEST.MF tools/RelocateAsm.java
	@test -n "$(ASM_JAR)" || { echo "Could not find Prism's org.ow2.asm:asm library" >&2; exit 1; }
	@test -n "$(ASM_COMMONS_JAR)" || { echo "Could not find Prism's org.ow2.asm:asm-commons library" >&2; exit 1; }
	@rm -rf build/agent-raw build/agent-classes build/shader-classes
	@mkdir -p build/agent-raw build/agent-classes build/shader-classes
	javac --release 17 -cp "$(ASM_JAR)" \
		-d build/agent-raw $(AGENT_SOURCES)
	unzip -q "$(ASM_JAR)" 'org/objectweb/asm/*' -d build/agent-raw
	javac --release 17 -cp "$(ASM_JAR):$(ASM_COMMONS_JAR)" \
		-d build/shader-classes tools/RelocateAsm.java
	java -cp "build/shader-classes:$(ASM_JAR):$(ASM_COMMONS_JAR)" \
		RelocateAsm build/agent-raw build/agent-classes
	jar --create --file $@ --manifest mcguard-agent/MANIFEST.MF -C build/agent-classes .

test: all
	python3 -m unittest discover -s tests -v
	./tests/test-agent-compatibility

clean:
	rm -rf build
