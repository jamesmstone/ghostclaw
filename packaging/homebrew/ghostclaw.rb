class Ghostclaw < Formula
  desc "Personal AI assistant: fast, secure, extensible"
  homepage "https://github.com/yourusername/ghostclaw"
  url "https://github.com/yourusername/ghostclaw/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "REPLACE_WITH_RELEASE_SHA256"
  license "MIT"

  depends_on "cmake" => :build
  depends_on "openssl@3"
  depends_on "curl"
  depends_on "sqlite"

  def install
    system "cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"
    system "cmake", "--build", "build", "--parallel"
    bin.install "build/ghostclaw"
  end

  test do
    assert_match "ghostclaw", shell_output("#{bin}/ghostclaw --version")
  end
end
