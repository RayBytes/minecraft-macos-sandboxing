package dev.rayan.mcguard;

import java.lang.instrument.ClassFileTransformer;
import java.security.ProtectionDomain;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

final class AuthlibTransformer implements ClassFileTransformer {
    static final String TARGET = "com/mojang/authlib/yggdrasil/YggdrasilMinecraftSessionService";
    static final String USER_API_TARGET = "com/mojang/authlib/yggdrasil/YggdrasilUserApiService";
    private static final String MODERN = "(Ljava/util/UUID;Ljava/lang/String;Ljava/lang/String;)V";
    private static final String LEGACY = "(Lcom/mojang/authlib/GameProfile;Ljava/lang/String;Ljava/lang/String;)V";

    @Override
    public byte[] transform(Module module, ClassLoader loader, String className,
                            Class<?> classBeingRedefined, ProtectionDomain protectionDomain,
                            byte[] classfileBuffer) {
        if (!TARGET.equals(className) && !USER_API_TARGET.equals(className)) {
            return null;
        }

        ClassReader reader = new ClassReader(classfileBuffer);
        ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_MAXS);
        boolean[] replaced = {false};
        reader.accept(new ClassVisitor(Opcodes.ASM8, writer) {
            @Override
            public MethodVisitor visitMethod(int access, String name, String descriptor,
                                             String signature, String[] exceptions) {
                MethodVisitor delegate = super.visitMethod(access, name, descriptor, signature, exceptions);
                boolean sessionJoin = TARGET.equals(className) && "joinServer".equals(name)
                    && (MODERN.equals(descriptor) || LEGACY.equals(descriptor));
                boolean profileKeys = USER_API_TARGET.equals(className) && "getKeyPair".equals(name)
                    && "()Lcom/mojang/authlib/yggdrasil/response/KeyPairResponse;".equals(descriptor);
                if (!sessionJoin && !profileKeys) {
                    return delegate;
                }
                replaced[0] = true;
                return sessionJoin ? joinReplacement(delegate, descriptor) : keyPairReplacement(delegate);
            }
        }, 0);

        if (!replaced[0] && TARGET.equals(className)) {
            Agent.fail("authlib session service has an unsupported joinServer method");
        }
        if (!replaced[0]) {
            return null;
        }
        return writer.toByteArray();
    }

    private static MethodVisitor joinReplacement(MethodVisitor output, String descriptor) {
        return new MethodVisitor(Opcodes.ASM8) {
            @Override
            public void visitEnd() {
                output.visitCode();
                output.visitVarInsn(Opcodes.ALOAD, 1);
                if (LEGACY.equals(descriptor)) {
                    output.visitMethodInsn(Opcodes.INVOKEVIRTUAL, "com/mojang/authlib/GameProfile", "getId",
                        "()Ljava/util/UUID;", false);
                }
                output.visitVarInsn(Opcodes.ALOAD, 3);
                output.visitMethodInsn(Opcodes.INVOKESTATIC, "dev/rayan/mcguard/BrokerClient", "joinServer",
                    "(Ljava/util/UUID;Ljava/lang/String;)V", false);
                output.visitInsn(Opcodes.RETURN);
                output.visitMaxs(2, 4);
                output.visitEnd();
            }
        };
    }

    private static MethodVisitor keyPairReplacement(MethodVisitor output) {
        return new MethodVisitor(Opcodes.ASM8) {
            @Override
            public void visitEnd() {
                output.visitCode();
                output.visitMethodInsn(Opcodes.INVOKESTATIC, "dev/rayan/mcguard/BrokerClient", "getKeyPair",
                    "()Ljava/lang/Object;", false);
                output.visitTypeInsn(Opcodes.CHECKCAST,
                    "com/mojang/authlib/yggdrasil/response/KeyPairResponse");
                output.visitInsn(Opcodes.ARETURN);
                output.visitMaxs(1, 1);
                output.visitEnd();
            }
        };
    }

}
