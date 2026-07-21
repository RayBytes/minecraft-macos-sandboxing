import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.commons.ClassRemapper;
import org.objectweb.asm.commons.Remapper;

public final class RelocateAsm {
    private static final String SOURCE = "org/objectweb/asm/";
    private static final String TARGET = "dev/rayan/mcguard/internal/asm/";

    private RelocateAsm() {}

    public static void main(String[] args) throws IOException {
        if (args.length != 2) {
            throw new IllegalArgumentException("usage: RelocateAsm INPUT_DIR OUTPUT_DIR");
        }
        Path input = Path.of(args[0]);
        Path output = Path.of(args[1]);
        Remapper remapper = new Remapper() {
            @Override
            public String map(String internalName) {
                return internalName.startsWith(SOURCE)
                    ? TARGET + internalName.substring(SOURCE.length())
                    : internalName;
            }
        };

        try (var paths = Files.walk(input)) {
            for (Path path : paths.filter(Files::isRegularFile).toList()) {
                Path relative = input.relativize(path);
                if (!relative.toString().endsWith(".class")) {
                    Path destination = output.resolve(relative);
                    Files.createDirectories(destination.getParent());
                    Files.copy(path, destination);
                    continue;
                }

                ClassReader reader = new ClassReader(Files.readAllBytes(path));
                ClassWriter writer = new ClassWriter(0);
                reader.accept(new ClassRemapper(writer, remapper), 0);
                String mappedName = remapper.map(reader.getClassName());
                Path destination = output.resolve(mappedName + ".class");
                Files.createDirectories(destination.getParent());
                Files.write(destination, writer.toByteArray());
            }
        }
    }
}
