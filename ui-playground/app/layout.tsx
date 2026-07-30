import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "EPW Interface Lab",
  description:
    "Interactive 240 × 240 e-paper smart watch UI playground.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
